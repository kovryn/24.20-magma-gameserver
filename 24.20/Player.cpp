#include "Player.h"
#include "Utils.h"
#include "Options.h"

using namespace Utils;

void Player::ServerAcknowledgePossession(AFortPlayerController* Controller, APawn* New)
{
    Controller->AcknowledgedPawn = New;
}

void (*ServerLoadingScreenDroppedOG)(AFortPlayerControllerAthena* Controller);
void Player::ServerLoadingScreenDropped(AFortPlayerControllerAthena* Controller)
{
    if (Controller->bLoadingScreenDropped) return;

    auto playerName = Controller->PlayerState->GetPlayerName().ToString();
    for (auto str : JoinedPlayers)
    {
        if (str == playerName)
        {
            Controller->ClientReturnToMainMenu(TEXT("You can't rejoin!"));
            return;
        }
    }

    auto PlayerState = (AFortPlayerStateAthena*)Controller->PlayerState;

    JoinedPlayers.push_back(PlayerState->GetPlayerName().ToString());

    static void* (*GetInterface)(void*, void*) = decltype(GetInterface)(Utils::GetAddr(0x00007FF73B49B844));

    TScriptInterface<IAbilitySystemInterface> a{};
    a.ObjectPointer = PlayerState;
    a.InterfacePointer = GetInterface(PlayerState, IAbilitySystemInterface::StaticClass());

    UFortKismetLibrary::EquipFortAbilitySet(a, StaticLoadObject<UFortAbilitySet>(TEXT("/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer")), nullptr);
    UFortKismetLibrary::EquipFortAbilitySet(a, StaticLoadObject<UFortAbilitySet>(TEXT("/TacticalSprintGame/Gameplay/AS_TacticalSprint.AS_TacticalSprint")), nullptr);

    FGameMemberInfo NewInfo{ -1,-1,-1 };
    NewInfo.MemberUniqueId = PlayerState->UniqueId;
    NewInfo.SquadId = PlayerState->SquadId;
    NewInfo.TeamIndex = PlayerState->TeamIndex;

    if (!Controller->BroadcastRemoteClientInfo)
    {
        Controller->BroadcastRemoteClientInfo = SpawnActor<AFortBroadcastRemoteClientInfo>({}, {}, Controller);
    }

    Controller->BroadcastRemoteClientInfo->bActive = true;
    Controller->BroadcastRemoteClientInfo->OnRep_bActive();
    Controller->BroadcastRemoteClientInfo->OnRep_bActive();

    auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
    auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;

    GameState->GameMemberInfoArray.Members.Add(NewInfo);
    GameState->GameMemberInfoArray.MarkItemDirty(NewInfo);

    static void(*ApplyCharacterCustomization)(void*, void*, void*) = decltype(ApplyCharacterCustomization)(Utils::GetAddr(0x00007FF73C49FC80));
    ApplyCharacterCustomization(Controller->PlayerState, ((AFortPlayerStateAthena*)Controller->PlayerState)->CustomPRIComponent, Controller->PlayerState->Owner);

    Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall")));
    Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor")));
    Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W")));
    Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS")));
    Inventory::GiveItem(Controller, StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/EditTool.EditTool")));

    Inventory::GiveItem(Controller, Controller->CosmeticLoadoutPC.Pickaxe->WeaponDefinition);

    Inventory::GiveItem(Controller, Wood, 9999);

    PlayerState->OnRep_TeamIndex(0);
    PlayerState->OnRep_SquadId();
    PlayerState->OnRep_PlayerTeam();
    PlayerState->OnRep_PlayerTeamPrivate();


    if (GameState->GamePhase >= EAthenaGamePhase::SafeZones && Controller->MyFortPawn)
    {
        Controller->MyFortPawn->SetShield(100);
        Controller->MyFortPawn->BeginSkydiving(false);

        Inventory::GiveItem(Controller, Wood, 500);
        Inventory::GiveItem(Controller, Stone, 500);
        Inventory::GiveItem(Controller, Metal, 500);

        Inventory::GiveItem(Controller, Shells, 15);
        Inventory::GiveItem(Controller, LightAmmo, 30);
        Inventory::GiveItem(Controller, MediumAmmo, 30);
        Inventory::GiveItem(Controller, HeavyAmmo, 6);

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

        }
    }

    return ServerLoadingScreenDroppedOG(Controller);
}

void Player::ServerAttemptAircraftJump(UFortControllerComponent_Aircraft* Comp, FRotator ClientRot)
{
    static bool First = false;

    if (!First)
    {
        First = true;

        auto loadout1 = new Loadout();

        loadout1->AddItem(StaticLoadObject<UFortItemDefinition>(TEXT("/RadicalWeaponsGameplay/Weapons/RadicalShotgunPump/WID_Shotgun_RadicalPump_Athena_UR.WID_Shotgun_RadicalPump_Athena_UR")), 1, 6);
        loadout1->AddItem(StaticLoadObject<UFortItemDefinition>(TEXT("/RadicalWeaponsGameplay/Weapons/PulseRifleMMObj/WID_Assault_PastaRipper_Athena_MMObj.WID_Assault_PastaRipper_Athena_MMObj")), 1, 20);
        loadout1->AddItem(StaticLoadObject<UFortItemDefinition>(TEXT("/FlipperGameplay/Items/HealSpray/WID_Athena_HealSpray.WID_Athena_HealSpray")), 1, 150);
        loadout1->AddItem(StaticLoadObject<UFortItemDefinition>(TEXT("/Game/Athena/Items/Consumables/PurpleStuff/Athena_PurpleStuff.Athena_PurpleStuff")), 1);
        loadout1->AddItem(StaticLoadObject<UFortItemDefinition>(TEXT("/KatanaGameplay/Items/Katana/WID_Melee_Katana.WID_Melee_Katana")), 1, 3);

        Loadouts.push_back(loadout1);
    }

    auto Controller = (AFortPlayerControllerAthena*)Comp->GetOwner();
    ((AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode)->RestartPlayer(Controller);
    auto PlayerState = (AFortPlayerStateAthena*)Controller->PlayerState;

    TArray<FFortItemEntry> Copy;
    Controller->WorldInventory->Inventory.ReplicatedEntries.Copy(&Copy);
    for (auto ItemEntry : Copy)
    {
        if (ItemEntry.ItemDefinition->IsA(UFortResourceItemDefinition::StaticClass()))
        {
            Inventory::RemoveItem(Controller, ItemEntry.ItemDefinition, ItemEntry.Count, ItemEntry.ItemGuid);
        }
    }

    Copy.Free();

    if (Loadouts.size() > 0)
    {
        auto RandomLoadout = Loadout::GetRandomLoadout();

        if (RandomLoadout)
        {
            for (auto& Pair : RandomLoadout->Items)
            {
                Inventory::GiveItem(Controller, Pair.first, Pair.second.first, Pair.second.second, UFortScalableFloatUtils::GetValueAtLevel(Pair.first->MaxStackSize, 0) > 1);
            }
        }
    }
    else
    {
    }

    if (!LightAmmo) LightAmmo = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsLight.AthenaAmmoDataBulletsLight");
    if (!MediumAmmo) MediumAmmo = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsMedium.AthenaAmmoDataBulletsMedium");
    if (!HeavyAmmo) HeavyAmmo = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsHeavy.AthenaAmmoDataBulletsHeavy");
    if (!Shells) Shells = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataShells.AthenaAmmoDataShells");

    if (!Wood) Wood = StaticLoadObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/WoodItemData.WoodItemData");
    if (!Stone) Stone = StaticLoadObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/StoneItemData.StoneItemData");
    if (!Metal) Metal = StaticLoadObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/MetalItemData.MetalItemData");

    Inventory::GiveItem(Controller, Wood, 500);
    Inventory::GiveItem(Controller, Stone, 500);
    Inventory::GiveItem(Controller, Metal, 500);

    Inventory::GiveItem(Controller, LightAmmo, 100);
    Inventory::GiveItem(Controller, MediumAmmo, 70);
    Inventory::GiveItem(Controller, HeavyAmmo, 10);
    Inventory::GiveItem(Controller, Shells, 10);
    Controller->MyFortPawn->BeginSkydiving(true);
}

void Player::ServerExecuteInventoryItem(AFortPlayerControllerAthena* Controller, FGuid ItemGuid)
{
    if (!Controller || !Controller->WorldInventory || !Controller->Pawn || ((AFortPlayerPawn*)Controller->Pawn)->IsDead() || !Controller->MyFortPawn || Controller->MyFortPawn->IsDead())  return;
    for (auto& item : Controller->WorldInventory->Inventory.ReplicatedEntries)
    {
        if (item.ItemGuid == ItemGuid)
        {
            if (item.ItemDefinition->IsA(UFortDecoItemDefinition::StaticClass()))
            {
                Controller->MyFortPawn->PickUpActor(nullptr, (UFortDecoItemDefinition*)item.ItemDefinition);
                Controller->MyFortPawn->CurrentWeapon->ItemEntryGuid = ItemGuid;

                if (Controller->MyFortPawn->CurrentWeapon->IsA(AFortDecoTool_ContextTrap::StaticClass()))
                {
                    reinterpret_cast<AFortDecoTool_ContextTrap*>(Controller->MyFortPawn->CurrentWeapon)->ContextTrapItemDefinition = (UFortContextTrapItemDefinition*)item.ItemDefinition;
                }
            }

            Controller->MyFortPawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)item.ItemDefinition, ItemGuid, item.TrackerGuid, false);
            break;
        }
    }
}

void (*GetPlayerViewPointAthenaOG)(AFortPlayerControllerAthena* PC, FVector& OutLoc, FRotator& OutRot);
void Player::GetPlayerViewPointAthena(AFortPlayerControllerAthena* PC, FVector& OutLoc, FRotator& OutRot)
{
    static FName PlayingName = UKismetStringLibrary::Conv_StringToName(TEXT("Playing"));
    auto ViewTarget = PC->GetViewTarget();
    if (PC->StateName.ComparisonIndex == PlayingName.ComparisonIndex && ViewTarget)
    {
        OutLoc = ViewTarget->K2_GetActorLocation();
        OutRot = PC->GetControlRotation();
    }
    else
    {
        GetPlayerViewPointAthenaOG(PC, OutLoc, OutRot);
    }
}


void (*EnterAircraftOG)(AFortPlayerControllerAthena* Controller, int64 a2);
void Player::EnterAircraft(AFortPlayerControllerAthena* Controller, int64 a2)
{
    EnterAircraftOG(Controller, a2);
    auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
    auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;

    static bool FirstPlayer = false;

    if (bLateGame && !FirstPlayer)
    {
        FirstPlayer = true;

        (new thread(SendWebhookMessage, "Game Started"))->detach();

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

void Player::ServerAttemptInventoryDrop(AFortPlayerControllerAthena* PlayerController, FGuid& ItemGuid, int32 Count, bool bTrash)
{
    FFortItemEntry ItemEntry = Inventory::GetItemEntry(PlayerController, ItemGuid);
    if (!ItemEntry.ItemDefinition) return;

    Inventory::SpawnPickup(PlayerController->MyFortPawn->K2_GetActorLocation(), ItemEntry);
    Inventory::RemoveItem(PlayerController, ItemEntry.ItemDefinition, Count, ItemGuid);
}

void Player::ServerHandlePickup(AFortPlayerPawnAthena* Pawn, AFortPickup* Pickup, FFortPickupRequestInfo Info)
{
    if (!Pickup || !Pawn || !Pawn->Controller || Pickup->bPickedUp) return;

    auto PC = ((AFortPlayerControllerAthena*)Pawn->Controller);
    SwapGuids[PC] = Pawn->CurrentWeapon ? Pawn->CurrentWeapon->GetInventoryGUID() : FGuid();

    Pickup->PickupLocationData.FlyTime = .4f;
    Pickup->PickupLocationData.ItemOwner.ObjectIndex = Pawn->Index;
    Pickup->PickupLocationData.ItemOwner.ObjectSerialNumber = UObject::GObjects->GetObjByIndex(Pawn->Index)->SerialNumber;
    Pickup->PickupLocationData.PickupTarget = Pickup->PickupLocationData.ItemOwner;
    Pickup->PickupLocationData.PickupGuid = Pickup->PrimaryPickupItemEntry.ItemGuid;
    Pickup->OnRep_PickupLocationData();
    Pickup->bPickedUp = true;
    Pickup->OnRep_bPickedUp();
    Inventory::DestroyPickup(Pickup);
}

void Player::Hook() {
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x130, ServerAcknowledgePossession, nullptr);
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x280, ServerLoadingScreenDropped, (void**)&ServerLoadingScreenDroppedOG);
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x22D, ServerExecuteInventoryItem, nullptr);
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x238, ServerAttemptInventoryDrop, nullptr);
    SwapVFTs(AFortPlayerPawnAthena::GetDefaultObj(), 0x234, ServerHandlePickup, nullptr);
    SwapVFTs(UFortControllerComponent_Aircraft::GetDefaultObj(), 0xA5, ServerAttemptAircraftJump, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF73B7609D0)), GetPlayerViewPointAthena, (void**)&GetPlayerViewPointAthenaOG);
}