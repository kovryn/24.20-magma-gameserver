#pragma once
#include "SDK.hpp"
#include <map>

using namespace SDK;
using namespace std;

inline map<AFortPlayerControllerAthena*, FGuid> SwapGuids{};

namespace Inventory
{
	void UpdateItem(AFortPlayerControllerAthena* PC, FFortItemEntry& Entry);
	void GiveItem(AFortPlayerControllerAthena* PC, UFortItemDefinition* Item, int Count = 1, int LoadedAmmo = 0, bool Stack = false);
	AFortPickup* SpawnPickup(FVector Loc, UFortItemDefinition* Def, EFortPickupSourceTypeFlag SourceTypeFlag, int Count, int LoadedAmmo, EFortPickupSpawnSource SpawnSource, AFortPlayerPawn* Owner = nullptr);
	void RemoveItem(AFortPlayerControllerAthena* PC, UFortItemDefinition* Def, int Count, FGuid ItemGuid = FGuid());

	bool CheckAndStack(AFortPlayerControllerAthena* PC, UFortItemDefinition* Def, int Count);

	void ServerHandlePickup(AFortPlayerPawnAthena* Pawn, AFortPickup* Pickup, FFortPickupRequestInfo Info);

	inline void(*DestroyPickupOG)(AFortPickup* Pickup);
	void DestroyPickup(AFortPickup* Pickup);

	FFortItemEntry& GetItemEntry(AFortPlayerControllerAthena* PlayerController, FGuid ItemGuid);

	__int64 RemoveItemHook(IFortInventoryOwnerInterface* a1, FGuid* ItemGuid, int Count, bool bForceRemoveFromQuickBars, bool bForceRemoval);

	int GetClipSize(UFortItemDefinition* Def);

	AFortPickupAthena* SpawnPickup(FVector Loc, FFortItemEntry& Entry);
}