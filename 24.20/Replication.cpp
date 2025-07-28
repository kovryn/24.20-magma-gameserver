#include "Replication.h"
#include <algorithm>
#include <chrono>
#include <random>
#include "Options.h"
using namespace Utils;

using namespace SDK;
using namespace std;
using namespace chrono;

#define WITH_SERVER_CODE 1
#define UE_WITH_IRIS 0

inline std::random_device rd;
inline std::mt19937 gen(rd());
inline std::uniform_real_distribution<> dis(0, 1);

float Rand()
{
	return dis(gen);
}

int32 ServerReplicateActors_PrepConnections(UNetDriver* Driver, const float DeltaSeconds)
{
	auto& ClientConnections = Driver->ClientConnections;

	int32 NumClientsToTick = Driver->ClientConnections.Num();

	/* Getting number of milliseconds as an integer. */
	bool bFoundReadyConnection = false;

	for (int32 ConnIdx = 0; ConnIdx < ClientConnections.Num(); ConnIdx++)
	{
		UNetConnection* Connection = ClientConnections[ConnIdx];
		if (!Connection)
			continue;
		if (Connection->GetConnectionState() != USOCK_Pending && Connection->GetConnectionState() != USOCK_Open && Connection->GetConnectionState() != USOCK_Closed)
			continue;
		//checkSlow(Connection->GetUChildConnection() == NULL);

		// Handle not ready channels.
		//@note: we cannot add Connection->IsNetReady(0) here to check for saturation, as if that's the case we still want to figure out the list of relevant actors
		//			to reset their NetUpdateTime so that they will get sent as soon as the connection is no longer saturated
		AActor* OwningActor = Connection->OwningActor;
		if (OwningActor != NULL && Connection->GetConnectionState() == USOCK_Open && (Connection->Driver->GetElapsedTime() - Connection->LastReceiveTime < 1.5))
		{
			//check(World == OwningActor->GetWorld());

			bFoundReadyConnection = true;

			// the view target is what the player controller is looking at OR the owning actor itself when using beacons
			AActor* DesiredViewTarget = OwningActor;
			if (Connection->PlayerController)
			{
				if (AActor* ViewTarget = Connection->PlayerController->GetViewTarget())
				{
					if (/*ViewTarget->GetWorld()*/true)
					{
						// It is safe to use the player controller's view target.
						DesiredViewTarget = ViewTarget;
					}
					//else
					//{
					//	// Log an error, since this means the view target for the player controller no longer has a valid world (this can happen
					//	// if the player controller's view target was in a sublevel instance that has been unloaded).
					//	UE_LOG(LogNet, Warning, TEXT("Player controller %s's view target (%s) no longer has a valid world! Was it unloaded as part a level instance?"),
					//		*Connection->PlayerController->GetName(), *ViewTarget->GetName());
					//}
				}
			}
			Connection->ViewTarget = DesiredViewTarget;

			for (int32 ChildIdx = 0; ChildIdx < Connection->Children.Num(); ChildIdx++)
			{
				UNetConnection* Child = Connection->Children[ChildIdx];
				APlayerController* ChildPlayerController = Child->PlayerController;
				AActor* DesiredChildViewTarget = Child->OwningActor;

				if (ChildPlayerController)
				{
					AActor* ChildViewTarget = ChildPlayerController->GetViewTarget();

					if (ChildViewTarget/* && ChildViewTarget->GetWorld()*/)
					{
						DesiredChildViewTarget = ChildViewTarget;
					}
				}

				Child->ViewTarget = DesiredChildViewTarget;
			}
		}
		else
		{
			Connection->ViewTarget = NULL;
			for (int32 ChildIdx = 0; ChildIdx < Connection->Children.Num(); ChildIdx++)
			{
				Connection->Children[ChildIdx]->ViewTarget = NULL;
			}
		}
	}

	return bFoundReadyConnection ? NumClientsToTick : 0;
}

struct FNetworkObjectInfo
{
	/** Pointer to the replicated actor. */
	AActor* Actor;

	/** WeakPtr to actor. This is cached here to prevent constantly constructing one when needed for (things like) keys in TMaps/TSets */
	TWeakObjectPtr<AActor> WeakActor;

	/** Next time to consider replicating the actor. Based on FPlatformTime::Seconds(). */
	double NextUpdateTime;

	/** Last absolute time in seconds since actor actually sent something during replication */
	double LastNetReplicateTime;

	/** Optimal delta between replication updates based on how frequently actor properties are actually changing */
	float OptimalNetUpdateDelta;

	/** Last time this actor was updated for replication via NextUpdateTime
	* @warning: internal net driver time, not related to WorldSettings.TimeSeconds */
	float LastNetUpdateTime;

	/** Is this object still pending a full net update due to clients that weren't able to replicate the actor at the time of LastNetUpdateTime */
	UINT32 bPendingNetUpdate : 1;

	/** Force this object to be considered relevant for at least one update */
	UINT32 bForceRelevantNextUpdate : 1;

	/** List of connections that this actor is dormant on */
	TSet<TWeakObjectPtr<UNetConnection>> DormantConnections;
	TSet<TWeakObjectPtr<UNetConnection>> RecentlyDormantConnections;

	~FNetworkObjectInfo() {
	}
};

template< class ObjectType>
class TSharedPtr
{
public:
	ObjectType* Object;

	int32 SharedReferenceCount;
	int32 WeakReferenceCount;

	FORCEINLINE ObjectType* Get()
	{
		return Object;
	}
	FORCEINLINE ObjectType* Get() const
	{
		return Object;
	}
	FORCEINLINE ObjectType& operator*()
	{
		return *Object;
	}
	FORCEINLINE const ObjectType& operator*() const
	{
		return *Object;
	}
	FORCEINLINE ObjectType* operator->()
	{
		return Object;
	}
	FORCEINLINE ObjectType* operator->() const
	{
		return Object;
	}
};

class FNetworkObjectList
{
public:
	typedef TSet<TSharedPtr<FNetworkObjectInfo>> FNetworkObjectSet;

	FNetworkObjectSet AllNetworkObjects;
	FNetworkObjectSet ActiveNetworkObjects;
	FNetworkObjectSet ObjectsDormantOnAllConnections;

	TMap<TWeakObjectPtr<UNetConnection>, int32> NumDormantObjectsPerConnection;
};

FNetworkObjectList& GetNetworkObjectList(UNetDriver* NetDriver)
{
	return *(*(TSharedPtr<FNetworkObjectList>*)(__int64(NetDriver) + 1840));
}

void ServerReplicateActors_BuildConsiderList(UNetDriver* Driver, TArray<FNetworkObjectInfo*>& OutConsiderList, const float ServerTickTime)
{
	auto World = Driver->World;
	int32 NumInitiallyDormant = 0;

	const bool bUseAdapativeNetFrequency = false;
	//const bool bUseAdapativeNetFrequency = IsAdaptiveNetUpdateFrequencyEnabled();
	auto TimeSeconds = UGameplayStatics::GetTimeSeconds(World);
	//TArray<AActor*> ActorsToRemove;

	for (const TSharedPtr<FNetworkObjectInfo>& ObjectInfo : GetNetworkObjectList(Driver).ActiveNetworkObjects)
	{
		FNetworkObjectInfo* ActorInfo = ObjectInfo.Get();

		if (!ActorInfo->bPendingNetUpdate && TimeSeconds <= ActorInfo->NextUpdateTime)
		{
			continue;		// It's not time for this actor to perform an update, skip it
		}
		
		AActor* Actor = ActorInfo->Actor;

		if (Actor->bActorIsBeingDestroyed)
		{
			// Actors aren't allowed to be placed in the NetworkObjectList if they are PendingKillPending.
			// Actors should also be unconditionally removed from the NetworkObjectList when UWorld::DestroyActor is called.
			// If this is happening, it means code is not destructing Actors properly, and that's not OK.
			//UE_LOG(LogNet, Warning, TEXT("Actor %s was found in the NetworkObjectList, but is PendingKillPending"), *Actor->GetName());
			//ActorsToRemove.Add(Actor);
			continue;
		}

		if (Actor->GetRemoteRole() == ENetRole::ROLE_None)
		{
			//ActorsToRemove.Add(Actor);
			continue;
		}

		//// (this can happen when using beacon net drivers for example)
		//if (Actor->NetDriverName.ComparisonIndex != Driver->NetDriverName.ComparisonIndex)
		//{
		//	continue;
		//}

		// Verify the actor is actually initialized (it might have been intentionally spawn deferred until a later frame)
		//if (!Actor->IsActorInitialized())
		//{
		//	continue;
		//}

		// Don't send actors that may still be streaming in or out
		ULevel* Level = Actor->GetLevel();
		//if (Level->HasVisibilityChangeRequestPending() || Level->bIsAssociatingLevel)
		//{
		//	continue;
		//}

		//if (Actor && Actor->IsNetStartupActor() && (Actor->NetDormancy == DORM_Initial))
		//{
		//	// This stat isn't that useful in its current form when using NetworkActors list
		//	// We'll want to track initially dormant actors some other way to track them with stats
		//	SCOPE_CYCLE_COUNTER(STAT_NetInitialDormantCheckTime);
		//	NumInitiallyDormant++;
		//	ActorsToRemove.Add(Actor);
		//	//UE_LOG(LogNetTraffic, Log, TEXT("Skipping Actor %s - its initially dormant!"), *Actor->GetName() );
		//	continue;
		//}

		bool(*NeedsLoadForClient)(AActor * Actor) = decltype(NeedsLoadForClient)(Actor->VTable[0x21]);

		if (!NeedsLoadForClient(Actor)) // We have no business sending this unless the client can load
			continue;
		//checkSlow(World == Actor->GetWorld());

		// Set defaults if this actor is replicating for first time
		if (ActorInfo->LastNetReplicateTime == 0)
		{
			ActorInfo->LastNetReplicateTime = TimeSeconds;
			ActorInfo->OptimalNetUpdateDelta = 1.0f / Actor->NetUpdateFrequency;
		}

		const float ScaleDownStartTime = 2.0f;
		const float ScaleDownTimeRange = 5.0f;

		const float LastReplicateDelta = TimeSeconds - ActorInfo->LastNetReplicateTime;

		if (LastReplicateDelta > ScaleDownStartTime)
		{
			if (Actor->MinNetUpdateFrequency == 0.0f)
			{
				Actor->MinNetUpdateFrequency = 2.0f;
			}

			// Calculate min delta (max rate actor will update), and max delta (slowest rate actor will update)
			const float MinOptimalDelta = 1.0f / Actor->NetUpdateFrequency;									  // Don't go faster than NetUpdateFrequency
			const float MaxOptimalDelta = max(1.0f / Actor->MinNetUpdateFrequency, MinOptimalDelta); // Don't go slower than MinNetUpdateFrequency (or NetUpdateFrequency if it's slower)

			// Interpolate between MinOptimalDelta/MaxOptimalDelta based on how long it's been since this actor actually sent anything
			const float Alpha = clamp((LastReplicateDelta - ScaleDownStartTime) / ScaleDownTimeRange, 0.0f, 1.0f);
			ActorInfo->OptimalNetUpdateDelta = lerp(MinOptimalDelta, MaxOptimalDelta, Alpha);
		}

		// Setup ActorInfo->NextUpdateTime, which will be the next time this actor will replicate properties to connections
		// NOTE - We don't do this if bPendingNetUpdate is true, since this means we're forcing an update due to at least one connection
		//	that wasn't to replicate previously (due to saturation, etc)
		// NOTE - This also means all other connections will force an update (even if they just updated, we should look into this)
		if (!ActorInfo->bPendingNetUpdate)
		{
			const float NextUpdateDelta = bUseAdapativeNetFrequency ? ActorInfo->OptimalNetUpdateDelta : 1.0f / Actor->NetUpdateFrequency;

			// then set the next update time
			ActorInfo->NextUpdateTime = TimeSeconds + Rand() * ServerTickTime + NextUpdateDelta;
			
			// and mark when the actor first requested an update
			//@note: using ElapsedTime because it's compared against UActorChannel.LastUpdateTime which also uses that value
			ActorInfo->LastNetUpdateTime = Driver->GetElapsedTime();
		}

		// and clear the pending update flag assuming all clients will be able to consider it
		ActorInfo->bPendingNetUpdate = false;

		// add it to the list to consider below
		// For performance reasons, make sure we don't resize the array. It should already be appropriately sized above!
		//ensure(OutConsiderList.Num() < OutConsiderList.Max());
		OutConsiderList.Add(ActorInfo);

		// Call PreReplication on all actors that will be considered
		static void (*CallPreReplication)(AActor*, UNetDriver*) = decltype(CallPreReplication)(Utils::GetAddr(0x00007FF742224374));
		CallPreReplication(Actor, Driver);
	}

	//for (AActor* Actor : ActorsToRemove)
	//{
	//	RemoveNetworkActor(Actor);
	//}
	//ActorsToRemove.Free();
}

FNetViewer::FNetViewer(UNetConnection* InConnection, float DeltaSeconds) :
	Connection(InConnection),
	InViewer(InConnection->PlayerController ? InConnection->PlayerController : InConnection->OwningActor),
	ViewTarget(InConnection->ViewTarget),
	ViewLocation(),
	ViewDir()
{
	if (!InConnection->OwningActor)
		return;
	if (InConnection->PlayerController && (InConnection->PlayerController != InConnection->OwningActor))
		return;

	APlayerController* ViewingController = InConnection->PlayerController;

	// Get viewer coordinates.
	ViewLocation = ViewTarget->K2_GetActorLocation();
	if (ViewingController)
	{
		FRotator ViewRotation = ViewingController->GetControlRotation();
		ViewingController->GetPlayerViewPoint(&ViewLocation, &ViewRotation);
		ViewDir = UKismetMathLibrary::Conv_RotatorToVector(ViewRotation);
	}
}

UActorChannel* FindChannel(UNetConnection* Connection, AActor* Actor)
{
	for (size_t i = 0; i < Connection->OpenChannels.Num(); i++)
	{
		auto Channel = Connection->OpenChannels[i];
		if (Channel->IsA(UActorChannel::StaticClass()) && ((UActorChannel*)Channel)->Actor == Actor)
			return (UActorChannel*)Channel;
	}

	return nullptr;
}

static FORCEINLINE bool IsActorRelevantToConnection(AActor* Actor, TArray<FNetViewer>& ConnectionViewers)
{
	//if (!Actor->RootComponent)
	//	return true;
	bool (*IsNetRelevantFor)(AActor*, AActor*, AActor*, FVector&) = decltype(IsNetRelevantFor)(Actor->VTable[0x9E]);
	for (int32 viewerIdx = 0; viewerIdx < ConnectionViewers.Num(); viewerIdx++)
	{
		if (IsNetRelevantFor(Actor, ConnectionViewers[viewerIdx].InViewer, ConnectionViewers[viewerIdx].ViewTarget, ConnectionViewers[viewerIdx].ViewLocation))
		{
			return true;
		}
	}

	return false;
}

AActor* GetNetOwner(AActor* Actor)
{
	if (Actor->IsA(APlayerController::StaticClass()) || Actor->IsA(APawn::StaticClass()))
		return Actor;
	if (Actor->IsA(AOnlineBeaconClient::StaticClass()))
		return ((AOnlineBeaconClient*)Actor)->BeaconOwner;
	return Actor->Owner;
}

bool IsRelevancyOwnerFor(AActor* _this, const AActor* ReplicatedActor, const AActor* ActorOwner, const AActor* ConnectionActor)
{
	return (ActorOwner == _this);
}

static FORCEINLINE UNetConnection* IsActorOwnedByAndRelevantToConnection(AActor* Actor, TArray<FNetViewer>& ConnectionViewers, bool& bOutHasNullViewTarget)
{
	const AActor* ActorOwner = GetNetOwner(Actor);

	bOutHasNullViewTarget = false;

	for (int i = 0; i < ConnectionViewers.Num(); i++)
	{
		UNetConnection* ViewerConnection = ConnectionViewers[i].Connection;

		if (ViewerConnection->ViewTarget == nullptr)
		{
			bOutHasNullViewTarget = true;
		}

		if (ActorOwner == ViewerConnection->PlayerController ||
			(ViewerConnection->PlayerController && ActorOwner == ViewerConnection->PlayerController->Pawn) ||
			(ViewerConnection->ViewTarget && IsRelevancyOwnerFor(ViewerConnection->ViewTarget, Actor, ActorOwner, ViewerConnection->OwningActor)))
		{
			return ViewerConnection;
		}
	}

	return nullptr;
}

struct testStr
{
	uint32				OpenAcked : 1;		// If OpenedLocally is true, this means we have acknowledged the packet we sent the bOpen bunch on. Otherwise, it means we have received the bOpen bunch from the server.
	uint32				Closing : 1;			// State of the channel.
	uint32				Dormant : 1;			// Channel is going dormant (it will close but the client will not destroy
	uint32				bIsReplicationPaused : 1;	// Replication is being paused, but channel will not be closed
	uint32				OpenTemporary : 1;	// Opened temporarily.
	uint32				Broken : 1;			// Has encountered errors and is ignoring subsequent packets.
	uint32				bTornOff : 1;			// Actor associated with this channel was torn off
	uint32				bPendingDormancy : 1;	// Channel wants to go dormant (it will check during tick if it can go dormant)
	uint32				bIsInDormancyHysteresis : 1; // Channel wants to go dormant, and is otherwise ready to become dormant, but is waiting for a timeout before doing so.
	uint32				bPausedUntilReliableACK : 1; // Unreliable property replication is paused until all reliables are ack'd.
	uint32				SentClosingBunch : 1;	// Set when sending closing bunch to avoid recursion in send-failure-close case.
	uint32				bPooled : 1;			// Set when placed in the actor channel pool
	uint32				OpenedLocally : 1;	// Whether channel was opened locally or by remote.
	uint32				bOpenedForCheckpoint : 1;	// Whether channel was opened by replay checkpoint recording
};

static FORCEINLINE bool ShouldActorGoDormant(AActor* Actor, TArray<FNetViewer>& ConnectionViewers, UActorChannel* Channel, float Time, bool bLowNetBandwidth)
{
	auto test = (testStr*)(__int64(Channel) + 0x0030);

	if (Actor->NetDormancy <= ENetDormancy::DORM_Awake || !Channel || test->bPendingDormancy || test->Dormant)
	{
		// Either shouldn't go dormant, or is already dormant
		return false;
	}
	bool (*GetNetDormancy)(AActor * _this, const FVector & ViewPos, const FVector & ViewDir, AActor * Viewer, AActor * ViewTarget, UActorChannel * InChannel, float Time, bool bLowBandwidth) = decltype(GetNetDormancy)(Actor->VTable[0x89]);
	if (Actor->NetDormancy == ENetDormancy::DORM_DormantPartial)
	{
		for (int32 viewerIdx = 0; viewerIdx < ConnectionViewers.Num(); viewerIdx++)
		{
			if (!GetNetDormancy(Actor, ConnectionViewers[viewerIdx].ViewLocation, ConnectionViewers[viewerIdx].ViewDir, ConnectionViewers[viewerIdx].InViewer, ConnectionViewers[viewerIdx].ViewTarget, Channel, Time, bLowNetBandwidth))
			{
				return false;
			}
		}
	}

	return true;
}

void ServerReplicateActors_Replicate(UNetDriver* Driver, UNetConnection* Connection, TArray<FNetViewer>& ConnectionViewers, TArray<FNetworkObjectInfo*>& ConsiderList)
{
	auto TimeSeconds = UGameplayStatics::GetTimeSeconds(Driver->World);
	
	for (FNetworkObjectInfo* ActorInfo : ConsiderList)
	{
		AActor* Actor = ActorInfo->Actor;

		UActorChannel* Channel = FindChannel(Connection, ActorInfo->WeakActor.Get());

		// Skip actor if not relevant and theres no channel already.
		// Historically Relevancy checks were deferred until after prioritization because they were expensive (line traces).
		// Relevancy is now cheap and we are dealing with larger lists of considered actors, so we want to keep the list of
		// prioritized actors low.
		if (!Channel)
		{
			bool (*IsLevelInitializedForActor)(void*, void*, void*) = decltype(IsLevelInitializedForActor)(Driver->VTable[0x87]);
			if (!IsLevelInitializedForActor(Driver, Actor, Connection))
			{
				// If the level this actor belongs to isn't loaded on client, don't bother sending
				continue;
			}

			if (!IsActorRelevantToConnection(Actor, ConnectionViewers))
			{
				// If not relevant (and we don't have a channel), skip
				continue;
			}
		}

		UNetConnection* PriorityConnection = Connection;

		if (Actor->bOnlyRelevantToOwner)
		{
			// This actor should be owned by a particular connection, see if that connection is the one passed in
			bool bHasNullViewTarget = false;

			PriorityConnection = IsActorOwnedByAndRelevantToConnection(Actor, ConnectionViewers, bHasNullViewTarget);

			if (PriorityConnection == nullptr)
			{
				// Not owned by this connection, if we have a channel, close it, and continue
				// NOTE - We won't close the channel if any connection has a NULL view target.
				//	This is to give all connections a chance to own it
				if (!bHasNullViewTarget && Channel != NULL && Driver->GetElapsedTime() - Channel->GetRelevantTime() >= Driver->RelevantTimeout)
				{
					Channel->Close(EChannelCloseReason::Relevancy);
				}

				// This connection doesn't own this actor
				continue;
			}
		}
		else/* if (GSetNetDormancyEnabled != 0)*/
		{
			bool found = false;
			// Skip Actor if dormant
			////printf("ActorInfo->DormantConnections: %d\n", ActorInfo->DormantConnections.Num());
			for (auto& WeakConnectionT : ActorInfo->DormantConnections)
			{
				////printf("%s\n", WeakConnectionT.Get()->GetName().c_str());
				if (WeakConnectionT.Get() == Connection)
				{
					found = true;
					break;
				}	
			}
			if (found)
			{
				////printf("Found dormant actor.\n");
				continue;
			}

			// See of actor wants to try and go dormant
			if (ShouldActorGoDormant(Actor, ConnectionViewers, Channel, Driver->GetElapsedTime(), false))
			{
				////printf("StartBecomingDormant %s\n", Actor->GetName().c_str());
				static void(*StartBecomingDormant)(void*) = decltype(StartBecomingDormant)(Utils::GetAddr(0x00007FF742374930));
				// Channel is marked to go dormant now once all properties have been replicated (but is not dormant yet)
				StartBecomingDormant(Channel);
			}
		}
		
		// Actor is relevant to this connection, add it to the list
		// NOTE - We use NetTag to make sure SentTemporaries didn't already mark this actor to be skipped
		static FName ActorName = UKismetStringLibrary::Conv_StringToName(TEXT("Actor"));
		//UActorChannel* Channel = FindChannel(Connection, Actor);
		if (!Channel || Channel->Actor) //make sure didn't just close this channel
		{
			AActor* Actor = ActorInfo->Actor;
			bool bIsRelevant = false;

			bool (*IsLevelInitializedForActor)(void*, void*, void*) = decltype(IsLevelInitializedForActor)(Driver->VTable[0x87]);
			const bool bLevelInitializedForActor = IsLevelInitializedForActor(Driver, Actor, Connection);

			// only check visibility on already visible actors every 1.0 + 0.5R seconds
			// bTearOff actors should never be checked
			if (bLevelInitializedForActor)
			{
				if (!Actor->bTearOff && (!Channel || Driver->GetElapsedTime() - Channel->GetRelevantTime() > 1.0))
				{
					if (IsActorRelevantToConnection(Actor, ConnectionViewers))
					{
						bIsRelevant = true;
					}
#if NET_DEBUG_RELEVANT_ACTORS
					else if (DebugRelevantActors)
					{
						LastNonRelevantActors.Add(Actor);
					}
#endif // NET_DEBUG_RELEVANT_ACTORS
				}
			}
			else
			{
				// Actor is no longer relevant because the world it is/was in is not loaded by client
				// exception: player controllers should never show up here
				//UE_LOG(LogNetTraffic, Log, TEXT("- Level not initialized for actor %s"), *Actor->GetName());
			}
				
			// if the actor is now relevant or was recently relevant
			const bool bIsRecentlyRelevant = bIsRelevant || (Channel && Driver->GetElapsedTime() - Channel->GetRelevantTime() < Driver->RelevantTimeout);

			if (bIsRecentlyRelevant)
			{
				//FinalRelevantCount++;

				//TOptional<FScopedActorRoleSwap> SwapGuard;
				//if (ActorInfo->bSwapRolesOnReplicate)
				//{
				//	SwapGuard = FScopedActorRoleSwap(Actor);
				//}
				static __int64 (*SetChannelActor)(UActorChannel*, AActor*, __int64) = decltype(SetChannelActor)(Utils::GetAddr(0x00007FF73B8BF900));
				static UChannel* (*CreateChannel)(UNetConnection*, FName*, int, int) = decltype(CreateChannel)(Utils::GetAddr(0x00007FF73B780FC0));
				// Find or create the channel for this actor.
				// we can't create the channel if the client is in a different world than we are
				// or the package map doesn't support the actor's class/archetype (or the actor itself in the case of serializable actors)
				// or it's an editor placed actor and the client hasn't initialized the level it's in
				if (Channel == NULL)
				{
					if (bLevelInitializedForActor)
					{
						// Create a new channel for this actor.
						Channel = (UActorChannel*)CreateChannel(Connection, &ActorName, 2, -1);
						if (Channel)
						{
							SetChannelActor(Channel, Actor, 0);
						}
					}
					// if we couldn't replicate it for a reason that should be temporary, and this Actor is updated very infrequently, make sure we update it again soon
					else if (Actor->NetUpdateFrequency < 1.0f)
					{
						//UE_LOG(LogNetTraffic, Log, TEXT("Unable to replicate %s"), *Actor->GetName());
						ActorInfo->NextUpdateTime = TimeSeconds + 0.2f * Rand();
					}
				}

				if (Channel)
				{
					// if it is relevant then mark the channel as relevant for a short amount of time
					if (bIsRelevant)
					{
						Channel->GetRelevantTime() = Driver->GetElapsedTime() + 0.5 * Rand();
					}
					// if the channel isn't saturated
					if (true)
					{
						// replicate the actor
						//UE_LOG(LogNetTraffic, Log, TEXT("- Replicate %s. %d"), *Actor->GetName(), PriorityActors[j]->Priority);

#if NET_DEBUG_RELEVANT_ACTORS
						if (DebugRelevantActors)
						{
							LastRelevantActors.Add(Actor);
						}
#endif // NET_DEBUG_RELEVANT_ACTORS

						//double ChannelLastNetUpdateTime = Channel->LastUpdateTime;
						static __int64 (*ReplicateActor)(UActorChannel*) = decltype(ReplicateActor)(Utils::GetAddr(0x00007FF7423712AC));
						if (ReplicateActor(Channel))
						{
#if USE_SERVER_PERF_COUNTERS	
							// A channel time of 0.0 means this is the first time the actor is being replicated, so we don't need to record it
							if (ChannelLastNetUpdateTime > 0.0)
							{
								Connection->GetActorsStarvedByClassTimeMap().FindOrAdd(Actor->GetClass()->GetName()).Add((World->RealTimeSeconds - ChannelLastNetUpdateTime) * 1000.0f);
							}
#endif

							//ActorUpdatesThisConnectionSent++;
							////printf("replicateactor %s\n", Channel->Actor->GetName().c_str());
#if NET_DEBUG_RELEVANT_ACTORS
							if (DebugRelevantActors)
							{
								LastSentActors.Add(Actor);
							}
#endif // NET_DEBUG_RELEVANT_ACTORS

							// Calculate min delta (max rate actor will upate), and max delta (slowest rate actor will update)
							const float MinOptimalDelta = 1.0f / Actor->NetUpdateFrequency;
							const float MaxOptimalDelta = max(1.0f / Actor->MinNetUpdateFrequency, MinOptimalDelta);
							const float DeltaBetweenReplications = (TimeSeconds - ActorInfo->LastNetReplicateTime);

							// Choose an optimal time, we choose 70% of the actual rate to allow frequency to go up if needed
							ActorInfo->OptimalNetUpdateDelta = clamp(DeltaBetweenReplications * 0.7f, MinOptimalDelta, MaxOptimalDelta);
							ActorInfo->LastNetReplicateTime = TimeSeconds;
						}
						//ActorUpdatesThisConnection++;
						//OutUpdated++;
					}
					else
					{
						//UE_LOG(LogNetTraffic, Log, TEXT("- Channel saturated, forcing pending update for %s"), *Actor->GetName());
						// otherwise force this actor to be considered in the next tick again
						Actor->ForceNetUpdate();
					}
					// second check for channel saturation
					//if (!Connection->IsNetReady(0))
					//{
					//	// We can bail out now since this connection is saturated, we'll return how far we got though
					//	GNumSaturatedConnections++;
					//	return j;
					//}
				}
			}

			// If the actor wasn't recently relevant, or if it was torn off, close the actor channel if it exists for this connection
			if ((!bIsRecentlyRelevant || Actor->bTearOff) && Channel != NULL)
			{
				// Non startup (map) actors have their channels closed immediately, which destroys them.
				// Startup actors get to keep their channels open.

				// Fixme: this should be a setting
				if (!bLevelInitializedForActor)
				{
					//UE_LOG(LogNetTraffic, Log, TEXT("- Closing channel for no longer relevant actor %s"), *Actor->GetName());
					Channel->Close(Actor->bTearOff ? EChannelCloseReason::TearOff : EChannelCloseReason::Relevancy);
				}
			}
		}

//		if (Actor->NetTag != NetTag)
//		{
//			UE_LOG(LogNetTraffic, Log, TEXT("Consider %s alwaysrelevant %d frequency %f "), *Actor->GetName(), Actor->bAlwaysRelevant, Actor->NetUpdateFrequency);
//
//			Actor->NetTag = NetTag;
//
//			OutPriorityList[FinalSortedCount] = FActorPriority(PriorityConnection, Channel, ActorInfo, ConnectionViewers, bLowNetBandwidth);
//			OutPriorityActors[FinalSortedCount] = OutPriorityList + FinalSortedCount;
//
//			FinalSortedCount++;
//
//#if NET_DEBUG_RELEVANT_ACTORS
//			if (DebugRelevantActors)
//			{
//				LastPrioritizedActors.Add(Actor);
//			}
//#endif // NET_DEBUG_RELEVANT_ACTORS
//		}
	}
}

class FNetworkGUID
{
public:
	uint32 Value;
};

struct FActorDestructionInfo
{
public:
	FActorDestructionInfo()
		: Reason(EChannelCloseReason::Destroyed)
		, bIgnoreDistanceCulling(false)
	{}
	
	TWeakObjectPtr<ULevel> Level;
	TWeakObjectPtr<UObject> ObjOuter;
	FVector DestroyedPosition;
	FNetworkGUID NetGUID;
	FString PathName;
	FName StreamingLevelName;
	EChannelCloseReason Reason;

	/** When true the destruction info data will be sent even if the viewers are not close to the actor */
	bool bIgnoreDistanceCulling;
};

TMap<FNetworkGUID, FActorDestructionInfo>& GetDestroyedStartupOrDormantActors(UNetDriver* Driver)
{
	return *(TMap<FNetworkGUID, FActorDestructionInfo>*)(__int64(Driver) + 0x2F8);
}

int32 ServerReplicateActors(UNetDriver* Driver, float DeltaSeconds)
{
	auto World = Driver->World;
	auto& ClientConnections = Driver->ClientConnections;

#if WITH_SERVER_CODE
	if (Driver->ClientConnections.Num() == 0)
	{
		return 0;
	}
	
	//if (Driver->ReplicationDriver)//rep graph
	//{
	//	static int32 (*RepDriverServerReplicateActors)(void*, float) = decltype(RepDriverServerReplicateActors)(Driver->ReplicationDriver->VTable[0x68]);
	//	return RepDriverServerReplicateActors(Driver->ReplicationDriver, DeltaSeconds);
	//}

	// Bump the ReplicationFrame value to invalidate any properties marked as "unchanged" for this frame.
	++ * (DWORD*)(__int64(Driver) + 0x438);

	int32 Updated = 0;

	const int32 NumClientsToTick = ServerReplicateActors_PrepConnections(Driver, DeltaSeconds);

	if (NumClientsToTick == 0)
	{
		// No connections are ready this frame
		return 0;
	}

	AWorldSettings* WorldSettings = World->K2_GetWorldSettings();

	bool bCPUSaturated = false;
	float ServerTickTime = 60.f;
	if (ServerTickTime == 0.f)
	{
		ServerTickTime = DeltaSeconds;
	}
	else
	{
		ServerTickTime = 1.f / ServerTickTime;
		bCPUSaturated = DeltaSeconds > 1.2f * ServerTickTime;
	}
	
	TArray<FNetworkObjectInfo*> ConsiderList;
	ConsiderList.Reserve(GetNetworkObjectList(Driver).ActiveNetworkObjects.Num());

	ServerReplicateActors_BuildConsiderList(Driver, ConsiderList, ServerTickTime);

	TSet<UNetConnection*> ConnectionsToClose;

	//FMemMark Mark(FMemStack::Get());

	for (int32 i = 0; i < ClientConnections.Num(); i++)
	{
		UNetConnection* Connection = ClientConnections[i];
		if (!Connection)
			continue;

		// if this client shouldn't be ticked this frame
		if (i >= NumClientsToTick)
		{
			//UE_LOG(LogNet, Log, TEXT("skipping update to %s"),*Connection->GetName());
			// then mark each considered actor as bPendingNetUpdate so that they will be considered again the next frame when the connection is actually ticked
			for (int32 ConsiderIdx = 0; ConsiderIdx < ConsiderList.Num(); ConsiderIdx++)
			{
				AActor* Actor = ConsiderList[ConsiderIdx]->Actor;
				// if the actor hasn't already been flagged by another connection,
				if (Actor != NULL && !ConsiderList[ConsiderIdx]->bPendingNetUpdate)
				{
					// find the channel
					UActorChannel* Channel = FindChannel(Connection, ConsiderList[ConsiderIdx]->WeakActor.Get());
					// and if the channel last update time doesn't match the last net update time for the actor
					if (Channel != NULL /*&& Channel->LastUpdateTime < ConsiderList[ConsiderIdx]->LastNetUpdateTimestamp*/)
					{
						//UE_LOG(LogNet, Log, TEXT("flagging %s for a future update"),*Actor->GetName());
						// flag it for a pending update
						ConsiderList[ConsiderIdx]->bPendingNetUpdate = true;
					}
				}
			}
			// clear the time sensitive flag to avoid sending an extra packet to this connection
			//Connection->TimeSensitive = false;
		}
		else if (Connection->ViewTarget)
		{

			const int32 LocalNumSaturated = 0;
			//const int32 LocalNumSaturated = GNumSaturatedConnections;

			// Make a list of viewers this connection should consider (this connection and children of this connection)
			TArray<FNetViewer>& ConnectionViewers = WorldSettings->ReplicationViewers;
			
			ConnectionViewers.Clear();
			ConnectionViewers.Add(FNetViewer(Connection, DeltaSeconds));
			//for (int32 ViewerIndex = 0; ViewerIndex < Connection->Children.Num(); ViewerIndex++)
			//{
			//	if (Connection->Children[ViewerIndex]->ViewTarget != NULL)
			//	{
			//		new(ConnectionViewers)FNetViewer(Connection->Children[ViewerIndex], DeltaSeconds);
			//	}
			//}
			static void(*SendClientAdjustment)(void*) = decltype(SendClientAdjustment)(Utils::GetAddr(0x00007FF7425E71F4));
			// send ClientAdjustment if necessary
			// we do this here so that we send a maximum of one per packet to that client; there is no value in stacking additional corrections
			if (Connection->PlayerController)
			{
				SendClientAdjustment(Connection->PlayerController);
			}

			//for (int32 ChildIdx = 0; ChildIdx < Connection->Children.Num(); ChildIdx++)
			//{
			//	if (Connection->Children[ChildIdx]->PlayerController != NULL)
			//	{
			//		Connection->Children[ChildIdx]->PlayerController->SendClientAdjustment();
			//	}
			//}

			//FMemMark RelevantActorMark(FMemStack::Get());
			
			//FActorPriority* PriorityList = NULL;
			//FActorPriority** PriorityActors = NULL;

			// Get a sorted list of actors for this connection
			//const int32 FinalSortedCount = ServerReplicateActors_PrioritizeActors(Connection, ConnectionViewers, ConsiderList, bCPUSaturated, PriorityList, PriorityActors);

			// Process the sorted list of actors for this connection
			//const int32 LastProcessedActor = ServerReplicateActors_ProcessPrioritizedActors(Connection, ConnectionViewers, PriorityActors, FinalSortedCount, Updated);

			ServerReplicateActors_Replicate(Driver, Connection, ConnectionViewers, ConsiderList);
			//vector<FActorDestructionInfo*> ToDestroy{};
			//for (auto& Pair : GetDestroyedStartupOrDormantActors(Driver))
			//{
			//	ToDestroy.push_back(&Pair.Value());
			//}

			//for (auto Info : ToDestroy)
			//{
			/*	static int64(*SendDestructionInfo)(void*, void*, void*) = decltype(SendDestructionInfo)(Utils::GetAddr(0x00007FF7424FDE68));
				SendDestructionInfo(Driver, Connection, Info);*/
			//	UBlueprintSetLibrary::Set_Remove((TSet<int32>&)GetDestroyedStartupOrDormantActors(Driver), (int32&)*Info);
			//}

			//
			//ToDestroy.clear();

			//FActorDestructionInfo* DInfo = GetDestroyedStartupOrDormantActors(Driver).Find(ConnDInfo);

			//// relevant actors that could not be processed this frame are marked to be considered for next frame
			//for (int32 k = LastProcessedActor; k < FinalSortedCount; k++)
			//{
			//	if (!PriorityActors[k]->ActorInfo)
			//	{
			//		// A deletion entry, skip it because we dont have anywhere to store a 'better give higher priority next time'
			//		continue;
			//	}

			//	AActor* Actor = PriorityActors[k]->ActorInfo->Actor;

			//	UActorChannel* Channel = PriorityActors[k]->Channel;

			//	//UE_LOG(LogNetTraffic, Verbose, TEXT("Saturated. %s"), *Actor->GetName());
			//	//if (Channel != NULL && Driver->GetElapsedTime() - Channel->RelevantTime <= 1.0)
			//	//{
			//	//	UE_LOG(LogNetTraffic, Log, TEXT(" Saturated. Mark %s NetUpdateTime to be checked for next tick"), *Actor->GetName());
			//	//	PriorityActors[k]->ActorInfo->bPendingNetUpdate = true;
			//	//}
			//	//else if (IsActorRelevantToConnection(Actor, ConnectionViewers))
			//	//{
			//	//	// If this actor was relevant but didn't get processed, force another update for next frame
			//	//	UE_LOG(LogNetTraffic, Log, TEXT(" Saturated. Mark %s NetUpdateTime to be checked for next tick"), *Actor->GetName());
			//	//	PriorityActors[k]->ActorInfo->bPendingNetUpdate = true;
			//	//	if (Channel != NULL)
			//	//	{
			//	//		Channel->RelevantTime = ElapsedTime + 0.5 * UpdateDelayRandomStream.FRand();
			//	//	}
			//	//}

			//	// If the actor was forced to relevant and didn't get processed, try again on the next update;
			//	//if (PriorityActors[k]->ActorInfo->ForceRelevantFrame >= Connection->LastProcessedFrame)
			//	//{
			//	//	PriorityActors[k]->ActorInfo->ForceRelevantFrame = ReplicationFrame + 1;
			//	//}
			//}
			//RelevantActorMark.Pop();

			ConnectionViewers.Clear();

			//Connection->LastProcessedFrame = ReplicationFrame;

			//const bool bWasSaturated = GNumSaturatedConnections > LocalNumSaturated;
			//Connection->TrackReplicationForAnalytics(bWasSaturated);
		}

		//if (Connection->GetPendingCloseDueToReplicationFailure())
		//{
		//	ConnectionsToClose.Add(Connection);
		//}
	}

	//// shuffle the list of connections if not all connections were ticked
	if (NumClientsToTick < ClientConnections.Num())
	{
		int32 NumConnectionsToMove = NumClientsToTick;
		while (NumConnectionsToMove > 0)
		{
			// move all the ticked connections to the end of the list so that the other connections are considered first for the next frame
			UNetConnection* Connection = ClientConnections[0];
			ClientConnections.Remove(0);
			ClientConnections.Add(Connection);
			NumConnectionsToMove--;
		}
	}
	//Mark.Pop();

#if NET_DEBUG_RELEVANT_ACTORS
	if (DebugRelevantActors)
	{
		PrintDebugRelevantActors();
		LastPrioritizedActors.Empty();
		LastSentActors.Empty();
		LastRelevantActors.Empty();
		LastNonRelevantActors.Empty();

		DebugRelevantActors = false;
	}
#endif // NET_DEBUG_RELEVANT_ACTORS

	//for (UNetConnection* ConnectionToClose : ConnectionsToClose)
	//{
	//	ConnectionToClose->Close();
	//}
	ConsiderList.Free();
	return Updated;
#else
	return 0;
#endif // WITH_SERVER_CODE
}

static void (*FlushDormancy)(UNetConnection* Connection, AActor* Actor) = decltype(FlushDormancy)(Utils::GetAddr(0x00007FF7424F122C));

void Replication::SetNetDormancy(AActor* Actor, ENetDormancy NewDormancy)
{
	auto World = UWorld::GetWorld();
	auto NetDriver = World->NetDriver;

	if (NetDriver)
	{
		Actor->NetDormancy = NewDormancy;
		if (NewDormancy <= ENetDormancy::DORM_Awake)
		{
			for (size_t i = 0; i < NetDriver->ClientConnections.Num(); i++)
			{
				auto Con = NetDriver->ClientConnections[i];
				if (!Con)
					continue;
				FlushDormancy(Con, Actor);
			}
		}
	}
}

void Replication::FlushNetDormancy(AActor* Actor)
{
	if (Actor->NetDormancy <= ENetDormancy::DORM_Awake)
	{
		return;
	}

	if (Actor->NetDormancy == ENetDormancy::DORM_Initial)
	{
		Actor->NetDormancy = ENetDormancy::DORM_DormantAll;
	}

	if (!Actor->bReplicates)
	{
		return;
	}

	auto World = UWorld::GetWorld();
	auto NetDriver = World->NetDriver;
	if (NetDriver)
	{
		for (size_t i = 0; i < NetDriver->ClientConnections.Num(); i++)
		{
			auto Con = NetDriver->ClientConnections[i];
			if (!Con)
				continue;
			FlushDormancy(Con, Actor);
		}
	}
}

void Replication::TickFlush(UNetDriver* Driver, float DeltaSeconds)
{
	if (Driver->ClientConnections.Num() > 0 && !bSkipServerReplicateActors)
	{
		// Update all clients.
#if WITH_SERVER_CODE
		int32 Updated = 0;
		
#if UE_WITH_IRIS//no iris rip
		if (Driver->ReplicationDriver)
		{
			UpdateReplicationViews();
			SendClientMoveAdjustments();
			ReplicationSystem->PreSendUpdate(DeltaSeconds);
		}
		else
#endif // UE_WITH_IRIS
		{
			Updated = ServerReplicateActors(Driver, DeltaSeconds);
		}

		//static int32 LastUpdateCount = 0;
		//// Only log the zero replicated actors once after replicating an actor
		//if ((LastUpdateCount && !Updated) || Updated)
		//{
		//	//printf("%s replicated %d actors\n", *GetDescription(), Updated);
		//}
		//LastUpdateCount = Updated;
#endif // WITH_SERVER_CODE
	}

	auto GM = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
	auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
	if (!GameState->SafeZoneIndicator)
		GameState->SafeZoneIndicator = GM->SafeZoneIndicator;

	static bool setup = false;

	if (!setup && GameState->SafeZoneIndicator)
	{
		setup = true;
		//GameState->OnRep_SafeZoneIndicator();

		//printf("SafeZoneIndicator: %s %d\n", GameState->SafeZoneIndicator->GetName().c_str(), GameState->SafeZoneIndicator->SafeZonePhases.Num());
		GM->SafeZoneIndicator = GameState->SafeZoneIndicator;

		/*FFortSafeZonePhaseInfo Phase{};
		Phase.Center = GM->SafeZoneLocations[0];
		Phase.Radius = 20000;
		Phase.DamageInfo.Damage = 1;
		Phase.ShrinkTime = 10;
		Phase.WaitTime = 5;

		FFortSafeZonePhaseInfo Phase2{};
		Phase2.Center = GM->SafeZoneLocations[1];
		Phase2.Radius = 15000;
		Phase2.DamageInfo.Damage = 1;
		Phase2.ShrinkTime = 10;
		Phase2.WaitTime = 5;

		FFortSafeZonePhaseInfo Phase3{};
		Phase3.Center = GM->SafeZoneLocations[2];
		Phase3.Radius = 10000;
		Phase3.DamageInfo.Damage = 1;
		Phase3.ShrinkTime = 10;
		Phase3.WaitTime = 5;

		FFortSafeZonePhaseInfo Phase4{};
		Phase4.Center = GM->SafeZoneLocations[3];
		Phase4.Radius = 9800;
		Phase4.DamageInfo.Damage = 1;
		Phase4.ShrinkTime = 10;
		Phase4.WaitTime = 5;

		FFortSafeZonePhaseInfo Phase5{};
		Phase5.Center = GM->SafeZoneLocations[4];
		Phase5.Radius = 9500;
		Phase5.DamageInfo.Damage = 1;
		Phase5.ShrinkTime = 10;
		Phase5.WaitTime = 5;

		FFortSafeZonePhaseInfo Phase6{};
		Phase6.Center = GM->SafeZoneLocations[5];
		Phase6.Radius = 9000;
		Phase6.DamageInfo.Damage = 1;
		Phase6.ShrinkTime = 10;
		Phase6.WaitTime = 5;

		TArray<FFortSafeZonePhaseInfo> Phases;
		Phases.Add(Phase);
		Phases.Add(Phase2);
		Phases.Add(Phase3);
		Phases.Add(Phase4);
		Phases.Add(Phase5);
		Phases.Add(Phase6);

		GM->GetSafeZoneIndicator()->SafeZonePhases = Phases;
		GM->GetSafeZoneIndicator()->PhaseCount = 6;
		GM->GetSafeZoneIndicator()->OnRep_PhaseCount();
		GM->GetSafeZoneIndicator()->OnSafeZoneStateChange(EFortSafeZoneState::Starting, true);*/
		//GM->HandlePostSafeZonePhaseChanged();

		FCurveTableRowHandle RadiusRow{};
		RadiusRow.CurveTable = StaticLoadObject<UCurveTable>(TEXT("/Game/Athena/Balance/DataTables/AthenaGameData.AthenaGameData"));
		RadiusRow.RowName = UKismetStringLibrary::Conv_StringToName(TEXT("Default.SafeZone.Radius"));

		FCurveTableRowHandle WaitTimeRow{};
		WaitTimeRow.CurveTable = StaticLoadObject<UCurveTable>(TEXT("/Game/Athena/Balance/DataTables/AthenaGameData.AthenaGameData"));
		WaitTimeRow.RowName = UKismetStringLibrary::Conv_StringToName(TEXT("Default.SafeZone.WaitTime"));

		FCurveTableRowHandle ShrinkTimeRow{};
		ShrinkTimeRow.CurveTable = StaticLoadObject<UCurveTable>(TEXT("/Game/Athena/Balance/DataTables/AthenaGameData.AthenaGameData"));
		ShrinkTimeRow.RowName = UKismetStringLibrary::Conv_StringToName(TEXT("Default.SafeZone.ShrinkTime"));

		FCurveTableRowHandle DamageRow{};
		DamageRow.CurveTable = StaticLoadObject<UCurveTable>(TEXT("/Game/Athena/Balance/DataTables/AthenaGameData.AthenaGameData"));
		DamageRow.RowName = UKismetStringLibrary::Conv_StringToName(TEXT("Default.SafeZone.Damage"));

		if (!bLateGame)
		{
			GM->SafeZoneIndicator->SafeZonePhases.Free();
			for (size_t i = 0; i < GM->SafeZoneLocations.Num(); i++)
			{

				FFortSafeZonePhaseInfo Info{};
				Info.Center = GM->SafeZoneLocations[i];
				UFortKismetLibrary::EvaluateCurveTableRow(RadiusRow, i, &Info.Radius, FString());
				UFortKismetLibrary::EvaluateCurveTableRow(WaitTimeRow, i, &Info.WaitTime, FString());
				UFortKismetLibrary::EvaluateCurveTableRow(ShrinkTimeRow, i, &Info.ShrinkTime, FString());
				UFortKismetLibrary::EvaluateCurveTableRow(DamageRow, i, &Info.DamageInfo.Damage, FString());

				Info.DamageInfo.bPercentageBasedDamage = false;
				Info.DamageInfo.Damage *= 100;

				GM->SafeZoneIndicator->SafeZonePhases.Add(Info);
			}

			GM->SafeZoneIndicator->PhaseCount = GM->SafeZoneLocations.Num();
			GM->SafeZoneIndicator->OnRep_PhaseCount();
		}
		else
		{
			GM->SafeZoneIndicator->SafeZonePhases.Free();

			FFortSafeZonePhaseInfo Info{};
			FFortSafeZonePhaseInfo Info2{};
			Info.Center = GM->SafeZoneLocations[4];
			Info.Radius = 8000;
			Info.DamageInfo.Damage = 5;
			Info2.Center = GM->SafeZoneLocations[4];
			Info2.Radius = 8000;
			Info2.DamageInfo.Damage = 5;
			Info2.ShrinkTime = 1000000;

			GM->SafeZoneIndicator->SafeZonePhases.Add(Info);
			GM->SafeZoneIndicator->SafeZonePhases.Add(Info2);

			GM->SafeZoneIndicator->PhaseCount = GM->SafeZoneIndicator->SafeZonePhases.Num();
			GM->SafeZoneIndicator->OnRep_PhaseCount();
		}

		//GM->SafeZoneIndicator->OnSafeZoneStateChange(EFortSafeZoneState::Starting, true);
	}
	auto TimeSeconds = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
	static int LastPickupPurgeTime = 0;
	static int LastBuildPurgeTime = 0;

	if (TimeSeconds > LastPickupPurgeTime)
	{
		LastPickupPurgeTime = TimeSeconds + 60;
		for (auto Pickup : Pickups)
		{
			Pickup->K2_DestroyActor();
		}
		Pickups.clear();
	}

	if (TimeSeconds > LastBuildPurgeTime)
	{
		LastBuildPurgeTime = TimeSeconds + 420;
		for (auto Pickup : Builds)
		{
			Pickup->K2_DestroyActor();
		}
		Builds.clear();
	}

	TickFlushOG(Driver, DeltaSeconds);

	static bool firstJoin = false;

	if (!firstJoin && Driver->ClientConnections.Num())
	{
		firstJoin = true;
	}

	if (firstJoin && Driver->ClientConnections.Num() <= 0)
	{
		firstJoin = false;
		((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->RestartGame();
	}
}

void Replication::Hook()
{
	MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73BC5A1E4)), Replication::SetNetDormancy, nullptr);
	MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73BD6F964)), Replication::FlushNetDormancy, nullptr);
	MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73B90B208)), Replication::TickFlush, (void**)&Replication::TickFlushOG);
}
