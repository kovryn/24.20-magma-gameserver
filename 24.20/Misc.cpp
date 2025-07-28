#include "Misc.h"
#include "Utils.h"
#include "Options.h"
using namespace Utils;

__int64 (*SetupAIBotServiceOG)(UAthenaAIServicePlayerBots* BotService);
__int64 Misc::SetupAIBotService(UAthenaAIServicePlayerBots* BotService)
{
    (new thread(SendWebhookMessage, "<@&1269258092743233557> server up, open 51.77.111.99"))->detach();
    return SetupAIBotServiceOG(BotService);
}

__int64 (*OmgOG)(UAthenaAIServicePlayerBots* a1, __int64 a2);
__int64 Misc::Omg(UAthenaAIServicePlayerBots * a1, __int64 a2)
{
    a1->bCheatBotDebugMiniMapEnabled = true;

    TArray<AActor*> Starts;
    UGameplayStatics::GetAllActorsOfClass(UWorld::GetWorld(), AFortPlayerStartWarmup::StaticClass(), &Starts);

    for (size_t i = 0; i < 99; i++)
    {
        auto Start = Starts[rand() % Starts.Num()];
        ((UAthenaAISystem*)UWorld::GetWorld()->AISystem)->AISpawner->RequestSpawn(UFortAthenaAISpawnerData::CreateComponentListFromClass(a1->DefaultBotAISpawnerData, a1), Start->GetTransform(), false);
    }

    Starts.Free();

    return OmgOG(a1, a2);
}

bool Misc::HasStreamingLevelsCompletedLoadingUnLoading()
{
    return 1;
}

bool Misc::Context()
{
    return true;
}

void (*InitForWorldOG)(UAthenaNavSystem* NavSystem, UWorld* World, EFNavigationSystemRunMode Mode);
void Misc::InitForWorld(UAthenaNavSystem* NavSystem, UWorld* World, EFNavigationSystemRunMode Mode)
{
    NavSystem->bAutoCreateNavigationData = true;
    NavSystem->bAllowAutoRebuild = true;

    ((UAthenaAISystem*)World->AISystem)->bNavigationReady = true;
    InitForWorldOG(NavSystem, World, Mode);
}


void (*ProcessEventClientOG)(UObject* Object, UFunction* Func, void* Params);
void Misc::ProcessEventClient(UObject* Object, UFunction* Func, void* Params)
{
    string FuncName = Func->GetName();

    if (FuncName == "ServerCreateBuildingActor")
    {
        auto Controller = (AFortPlayerControllerAthena*)Object;
        auto b = (Params::FortPlayerController_ServerCreateBuildingActor*)Params;
        Controller->BroadcastRemoteClientInfo->ServerSetPlayerBuildableClass(Controller->CurrentBuildableClass);
    }
    else if (FuncName == "OnRep_DeathInfo")
    {
        if (!UFortEngine::GetEngine() || !UFortEngine::GetEngine()->GameInstance || !UFortEngine::GetEngine()->GameInstance->LocalPlayers.IsValidIndex(0) || !UFortEngine::GetEngine()->GameInstance->LocalPlayers[0]->PlayerController || !((AFortPlayerControllerAthena*)UFortEngine::GetEngine()->GameInstance->LocalPlayers[0]->PlayerController)->MyFortPawn)
            return ProcessEventClientOG(Object, Func, Params);

        auto PlayerState = (AFortPlayerStateAthena*)Object;

        if (!PlayerState || !PlayerState->DeathInfo.bInitialized)
            return ProcessEventClientOG(Object, Func, Params);

        auto Pawn = (AFortPlayerPawn*)PlayerState->GetPawn();
        if (!Pawn)
            Pawn = (AFortPlayerPawn*)PlayerState->DeathInfo.FinisherOrDowner.Get();

        if (!Pawn || !Pawn->IsA(AFortPlayerPawnAthena::StaticClass()))
            return ProcessEventClientOG(Object, Func, Params);

        if (Pawn->CurrentWeapon)
            Pawn->CurrentWeapon->K2_DestroyActor();

        auto Funca = Pawn->Class->GetFunction("PlayerPawn_Athena_C", "OnDeathPlayEffects");
        Params::FortPawn_OnDeathPlayEffects prms{};
        prms.Damage = 200;
        prms.DamageCauser = Pawn;
        prms.DamageTags = PlayerState->DeathInfo.DeathTags;
        prms.InstigatedBy = Pawn;
        ProcessEventClientOG(Pawn, Funca, &prms);
    }


    return ProcessEventClientOG(Object, Func, Params);
}


void (*ProcessEventOG)(UObject* Obj, UFunction* Func, void* Params);
void Misc::ProcessEvent(UObject* Obj, UFunction* Func, void* Params)
{
    auto FuncName = Func->GetName();

    if (FuncName == "OnAircraftExitedDropZone")
    {
        auto GameMode = (AFortGameModeAthena*)Obj;
        auto GameState = (AFortGameStateAthena*)GameMode->GameState;
        for (auto Player : GameMode->AlivePlayers)
        {
            if (Player->IsInAircraft())
            {
                Player->GetAircraftComponent()->ServerAttemptAircraftJump(FRotator());
            }
        }
    }
    else if (FuncName == "TeleportPlayerPawn")
    {
        auto prms = (Params::FortMissionLibrary_TeleportPlayerPawn*)Params;
        prms->ReturnValue = prms->PlayerPawn->K2_TeleportTo(prms->DestLocation, prms->DestRotation);
        prms->PlayerPawn->BeginSkydiving(false);
    }
    else if (FuncName == "ServerClientIsReadyToRespawn")
    {
        ProcessEventOG(Obj, Func, Params);
        auto Controller = ((AFortPlayerControllerAthena*)Obj);

        Controller->WorldInventory->Inventory.ReplicatedEntries.Free();
        Controller->WorldInventory->Inventory.ItemInstances.Free();

        Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall")));
        Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor")));
        Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W")));
        Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS")));
        Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/EditTool.EditTool")));

        Inventory::GiveItem(Controller, Controller->CosmeticLoadoutPC.Pickaxe->WeaponDefinition);

        if (!((AFortPlayerStateAthena*)Controller->PlayerState)->Place && ((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->IsRespawningAllowed((AFortPlayerStateAthena*)Controller->PlayerState))
        {
            FVector Loc = ((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->SafeZoneIndicator->GetSafeZoneCenter() + UKismetMathLibrary::RandomFloatInRange(-2500, 2500);
            Loc.Z = 15000;
            auto Pawn = SpawnActor<AFortPlayerPawnAthena>(Loc, {}, Controller, APlayerPawn_Athena_C::StaticClass());
            Controller->Possess(Pawn);
            Controller->RespawnPlayerAfterDeath(false);
            if (Pawn)
                Pawn->BeginSkydiving(false);

            Pawn->SetHealth(100);
            Pawn->SetShield(100);

            Inventory::GiveItem(Controller, Wood, 500);
            Inventory::GiveItem(Controller, Stone, 500);
            Inventory::GiveItem(Controller, Metal, 500);

            Inventory::GiveItem(Controller, Shells, 150);
            Inventory::GiveItem(Controller, LightAmmo, 200);
            Inventory::GiveItem(Controller, MediumAmmo, 200);
            Inventory::GiveItem(Controller, HeavyAmmo, 100);

            if (Loadouts.size() > 0)
            {
                auto RandomLoadout = Loadout::GetRandomLoadout();

                if (RandomLoadout)
                {
                    for (auto& Pair : RandomLoadout->Items)
                    {
                        if (Pair.first)
                        {
                            Inventory::GiveItem(Controller, Pair.first, Pair.second.first, Pair.second.second, Pair.first->MaxStackSize.Curve.CurveTable && UFortScalableFloatUtils::GetValueAtLevel(Pair.first->MaxStackSize, 0) > 1);
                        }
                    }
                }
            }
            else
            {
                //printf("Blurr you are actually gay you didn't setup any loadouts.");
            }

            static void* (*GetInterface)(void*, void*) = decltype(GetInterface)(Utils::GetAddr(0x00007FF73B49B844));

            TScriptInterface<IAbilitySystemInterface> a{};
            a.ObjectPointer = Controller->PlayerState;
            a.InterfacePointer = GetInterface(Controller->PlayerState, IAbilitySystemInterface::StaticClass());

            UFortKismetLibrary::EquipFortAbilitySet(a, StaticLoadObject<UFortAbilitySet>(TEXT("/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer")), nullptr);
            UFortKismetLibrary::EquipFortAbilitySet(a, StaticLoadObject<UFortAbilitySet>(TEXT("/TacticalSprintGame/Gameplay/AS_TacticalSprint.AS_TacticalSprint")), nullptr);
        }
        return;
    }
    else if (FuncName == "ClientOnPawnDied")
    {
        auto DeadPC = (AFortPlayerControllerAthena*)Obj;
        auto prms = (Params::FortPlayerControllerZone_ClientOnPawnDied*)Params;
        auto KillerState = (AFortPlayerStateAthena*)prms->DeathReport.KillerPlayerState;
        auto KillerPawn = (AFortPlayerPawnAthena*)prms->DeathReport.KillerPawn.Get();
        auto DeadState = (AFortPlayerStateAthena*)DeadPC->PlayerState;

        if (DeadState && KillerState && KillerState != DeadState)
        {
            KillerState->KillScore++;
            for (auto Member : KillerState->PlayerTeam->TeamMembers)
            {
                ((AFortPlayerStateAthena*)Member->PlayerState)->TeamKillScore++;
                ((AFortPlayerStateAthena*)Member->PlayerState)->OnRep_TeamKillScore();
                ((AFortPlayerStateAthena*)Member->PlayerState)->ClientReportTeamKill(1);
            }

            std::string url = "http://26.141.107.213:3551/sessions/api/v1/vbucks/" + std::string(KillerState->GetPlayerName().ToString()) + "/100";
            std::string url2 = "http://26.141.107.213:3551/sessions/api/v1/hype/" + std::string(KillerState->GetPlayerName().ToString()) + "/Kill";

            API::SendGet(url);
            API::SendGet(url2);
            KillerState->OnRep_Kills();
            KillerState->ClientReportKill(DeadState);
            KillerState->ClientReportTeamKill(1);
            auto KillerPC = (AFortPlayerControllerAthena*)KillerState->Owner;
            static FGameplayTag EarnedElim = { UKismetStringLibrary::Conv_StringToName(TEXT("Event.EarnedElimination")) };
            FGameplayEventData Data{};
            Data.EventTag = EarnedElim;
            Data.ContextHandle = KillerState->AbilitySystemComponent->MakeEffectContext();
            Data.Instigator = KillerState->Owner;
            Data.Target = DeadState;
            Data.TargetData = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromActor(DeadState);
            UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(KillerPC->Pawn, EarnedElim, Data);
        }

        if (DeadPC->Pawn && DeadPC->WorldInventory)
        {
            for (auto& Entry : DeadPC->WorldInventory->Inventory.ReplicatedEntries)
            {
                if (((UFortWorldItemDefinition*)Entry.ItemDefinition)->bCanBeDropped == 1)
                {
                    Inventory::SpawnPickup(DeadPC->Pawn->K2_GetActorLocation(), Entry);
                }
            }

            ProcessEventOG(Obj, Func, Params);
            static int64(*SendDestructionInfo)(void*, void*, void*) = decltype(SendDestructionInfo)(Utils::GetAddr(0x00007FF7424FDE68));
            static void* (*CreateDestructionInfo)(void*, void*, void*) = decltype(CreateDestructionInfo)(Utils::GetAddr(0x00007FF7424FDE68));
           
            DeadPC->MyFortPawn = ((AFortPlayerPawnAthena*)DeadPC->Pawn);
            DeadPC->OnFortPlayerPawnAthenaDied(((AFortPlayerPawnAthena*)DeadPC->Pawn));

            DeadState->DeathInfo.bInitialized = true;
            DeadState->DeathInfo.DeathTags = prms->DeathReport.Tags;
            DeadState->DeathInfo.FinisherOrDowner.ObjectIndex = DeadPC->Pawn->Index;
            DeadState->DeathInfo.FinisherOrDowner.ObjectSerialNumber = UObject::GObjects->GetObjByIndex(DeadPC->Pawn->Index)->SerialNumber;
            DeadState->OnRep_DeathInfo();

            return;
        }
    }

    return ProcessEventOG(Obj, Func, Params);
}

__int64 (*DispatchRequestOG)(__int64 a1, __int64* a2, int a3);
__int64 Misc::DispatchRequest(__int64 a1, __int64* a2, int a3)
{
    return DispatchRequestOG(a1, a2, 3);
}

__int64 __fastcall Misc::NetMode(__int64)
{
    return 1;
}

float Misc::GetMaxTickRate()
{
    return 60.f;
}

void (*SetGamePhaseOG)(AFortGameStateAthena* GameState, __int64 a2);
void Misc::SetGamePhase(AFortGameStateAthena* GameState, __int64 a2)
{
    SetGamePhaseOG(GameState, a2);
}

void (*HandlePostSafeZonePhaseChangedOG)(AFortGameModeAthena* GameMode);
void Misc::HandlePostSafeZonePhaseChanged(AFortGameModeAthena* GameMode)
{
    HandlePostSafeZonePhaseChangedOG(GameMode);

    auto Indicator = GameMode->SafeZoneIndicator;

    if (!Indicator) return;
}


void Misc::AdvancePhase(AFortSafeZoneIndicator* Indicator)
{
    if (Indicator->CurrentPhase + 1 > Indicator->PhaseCount - 1) return;

    auto& NextInfo = Indicator->SafeZonePhases.IsValidIndex(Indicator->CurrentPhase + 1) ? Indicator->SafeZonePhases[Indicator->CurrentPhase + 1] : Indicator->SafeZonePhases[Indicator->CurrentPhase];
    auto TimeSeconds = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());

    if (Indicator->SafeZonePhases.IsValidIndex(Indicator->CurrentPhase))
    {
        auto& a = Indicator->SafeZonePhases[Indicator->CurrentPhase];
        Indicator->Radius = a.Radius;
        Indicator->PreviousCenter = FVector_NetQuantize100(a.Center);
        Indicator->PreviousRadius = a.Radius;
        Indicator->SetSafeZoneRadiusAndCenter(a.Radius, a.Center);
    }

    Indicator->NextCenter = FVector_NetQuantize100(NextInfo.Center);
    Indicator->NextRadius = NextInfo.Radius;
    Indicator->CurrentPhase++;
    Indicator->OnRep_CurrentPhase();

    Indicator->CurrentDamageInfo = NextInfo.DamageInfo;
    Indicator->OnRep_CurrentDamageInfo();

    Indicator->SafeZoneStartShrinkTime = TimeSeconds + NextInfo.WaitTime;
    Indicator->SafeZoneFinishShrinkTime = Indicator->SafeZoneStartShrinkTime + NextInfo.ShrinkTime;
}

void (*UpdateSafeZonesPhaseOG)(AFortGameModeAthena* GameMode);
void Misc::UpdateSafeZonesPhase(AFortGameModeAthena* GameMode)
{
    UpdateSafeZonesPhaseOG(GameMode);

    auto GameState = (AFortGameStateAthena*)GameMode->GameState;
    auto TimeSeconds = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
    if (GameState->GamePhase == EAthenaGamePhase::SafeZones)
    {
        if (!GameState->SafeZoneIndicator) return;
        auto Indicator = GameState->SafeZoneIndicator;

        if (Indicator->CurrentPhase == -1)
        {
            AdvancePhase(Indicator);
            Indicator->SetSafeZoneRadiusAndCenter(185000, FVector());
        }
        else if (TimeSeconds >= Indicator->SafeZoneFinishShrinkTime)
        {
            AdvancePhase(Indicator);
        }
    }
}

static void (*EnterAircraftOG)(AFortPlayerControllerAthena* Controller, int64 a2);
void Misc::EnterAircraft(AFortPlayerControllerAthena* Controller, int64 a2)
{
    EnterAircraftOG(Controller, a2);
    auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
    auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
    static bool FirstPlayerInAircraft = false;
    if (bLateGame && !FirstPlayerInAircraft)
    {
        FirstPlayerInAircraft = true;
        (new thread(SendWebhookMessage, "Battlebus started"))->detach();

        GameState->SafeZonesStartTime = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()) + 2;


        auto Aircraft = GameState->GetAircraft(0);
        auto& FlightInfo = Aircraft->FlightInfo;
        FlightInfo.FlightSpeed = 1200;
        FlightInfo.FlightStartLocation = FVector_NetQuantize100(GameMode->SafeZoneLocations[4]);
        FlightInfo.FlightStartLocation.Z = 15000;
        FlightInfo.TimeTillDropEnd = 2;
        FlightInfo.TimeTillDropStart = 0;
        FlightInfo.TimeTillFlightEnd = 10;
        Aircraft->DropEndTime = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()) + 2;
        Aircraft->DropStartTime = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
        Aircraft->FlightEndTime = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()) + 10;
        GameState->bAircraftIsLocked = false;
        GameState->SafeZonesStartTime = UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()) + 2;
    }
}


void Misc::Hook()
{
    SwapInstruction(Utils::GetAddr(0x00007FF73FB5AD58), 1, 0x85);
    SwapInstruction(Utils::GetAddr(0x00007FF743E4F4B3), 1, 0x85);
    SwapInstruction(Utils::GetAddr(0x00007FF743E4F4CD), 1, 0x85);
    SwapInstruction(Utils::GetAddr(0x00007FF743E4F4C0), 1, 0x87);
    SwapInstruction(Utils::GetAddr(0x00007FF73B8C1D04), 1, 0x88);
    SwapInstruction(Utils::GetAddr(0x00007FF7458959EC), 1, 0x89);
    SwapInstruction(Utils::GetAddr(0x00007FF7451355A4), 0, 0x74);
    SwapInstruction(Utils::GetAddr(0x00007FF74513559B), 0, 0x7E);

    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73C45CD64)), Utils::ReturnZero, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73C45CDA4)), Utils::ReturnZero, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73D6EEF70)), Utils::ReturnZero, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF7423EAFAC)), Utils::ReturnZero, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF7404EECB0)), Utils::ReturnZero, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF7404EF9D0)), Utils::ReturnZero, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF743E542B8)), Utils::ReturnZero, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF744381278)), Context, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF742A71E4C)), Context, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73B35FB18)), Context, nullptr);

    SwapVFTs(AAthenaNavMesh::GetDefaultObj(), 0x21, Context, nullptr);
    SwapVFTs(UFortEngine::GetEngine(), 0x62, GetMaxTickRate, nullptr);

    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73B4B55EC)), NetMode, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73B6A6EF4)), NetMode, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73B6A6F60)), NetMode, nullptr);
    MH_CreateHook((void*)(InSDKUtils::GetImageBase() + Offsets::ProcessEvent), ProcessEvent, (void**)&ProcessEventOG);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF7451313D8)), SetupAIBotService, (void**)&SetupAIBotServiceOG);

    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF743E60A74)), UpdateSafeZonesPhase, (void**)&UpdateSafeZonesPhaseOG);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF743E4BA94)), HandlePostSafeZonePhaseChanged, (void**)&HandlePostSafeZonePhaseChangedOG);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73BB5F7C0)), DispatchRequest, (void**)&DispatchRequestOG);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF7442E13BC)), EnterAircraft, (void**)&EnterAircraftOG);

    MH_CreateHook((void*)(InSDKUtils::GetImageBase() + Offsets::ProcessEvent), ProcessEventClient, (void**)&ProcessEventClientOG);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF743EDF8CC)), HasStreamingLevelsCompletedLoadingUnLoading, nullptr);
}