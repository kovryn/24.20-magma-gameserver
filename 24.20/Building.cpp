#include "Building.h"
using namespace Utils;

void Building::ServerCreateBuildingActor(AFortPlayerControllerAthena* PC, FCreateBuildingActorData Data)
{
    static int64(*CantBuild)(UObject*, UObject*, FVector, FRotator, char, TArray<ABuildingSMActor*>*, char*) = decltype(CantBuild)(Utils::GetAddr(0x00007FF744603414));

    if (!PC->BroadcastRemoteClientInfo->RemoteBuildableClass) PC->BroadcastRemoteClientInfo->RemoteBuildableClass = Data.BuildingClassData.BuildingClass;

    auto GM = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;
    auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;

    if (!PC || !PC->BroadcastRemoteClientInfo || !PC->BroadcastRemoteClientInfo->RemoteBuildableClass.Get())
    {
        return;
    }

    Data.BuildingClassData.BuildingClass.ClassPtr = PC->BroadcastRemoteClientInfo->RemoteBuildableClass.Get();

    TArray<ABuildingSMActor*> ExistingBuildings;
    char crash;
    if (CantBuild(UWorld::GetWorld(), Data.BuildingClassData.BuildingClass, Data.BuildLoc, Data.BuildRot, Data.bMirrored, &ExistingBuildings, &crash))
    {
        ExistingBuildings.Free();
        return;
    }

    auto NewBuild = SpawnActor<ABuildingSMActor>(Data.BuildLoc, Data.BuildRot, nullptr, Data.BuildingClassData.BuildingClass.Get());
    if (!NewBuild) return;

    NewBuild->bPlayerPlaced = true;
    NewBuild->Team = EFortTeam(((AFortPlayerStateAthena*)PC->PlayerState)->TeamIndex);
    NewBuild->TeamIndex = ((AFortPlayerStateAthena*)PC->PlayerState)->TeamIndex;
    NewBuild->OnRep_Team();

    NewBuild->InitializeKismetSpawnedBuildingActor(NewBuild, PC, true, nullptr);

    for (auto Building : ExistingBuildings)
    {
        Building->K2_DestroyActor();
    }

    auto Def = UFortKismetLibrary::K2_GetResourceItemDefinition(NewBuild->ResourceType);

    ExistingBuildings.Free();

    Builds.push_back(NewBuild);
}

static ABuildingSMActor* (*ReplaceBuildingActor)(ABuildingSMActor*, __int64, UClass*, int, int, bool, AFortPlayerControllerAthena*) = decltype(ReplaceBuildingActor)(Utils::GetAddr(0x00007FF7441EB934));
void Building::ServerEditBuildingActor(AFortPlayerControllerAthena* PC, ABuildingSMActor* BuildingActorToEdit, TSubclassOf<ABuildingSMActor> NewBuildingClass, uint8 RotationIterations, bool bMirrored)
{
    auto PlayerState = (AFortPlayerStateAthena*)PC->PlayerState;

    if (!BuildingActorToEdit || !NewBuildingClass || BuildingActorToEdit->bDestroyed || BuildingActorToEdit->EditingPlayer != PlayerState)
        return;

    BuildingActorToEdit->SetNetDormancy(ENetDormancy::DORM_DormantAll);
    BuildingActorToEdit->EditingPlayer = nullptr;

    if (auto BuildingActor = ReplaceBuildingActor(BuildingActorToEdit, 1, NewBuildingClass, BuildingActorToEdit->CurrentBuildingLevel, RotationIterations, bMirrored, PC))
    {
        BuildingActor->bPlayerPlaced = true;

        BuildingActor->TeamIndex = PlayerState->TeamIndex;
        BuildingActor->Team = EFortTeam(PlayerState->TeamIndex);
        BuildingActor->OnRep_Team();
    }
}

void Building::ServerEndEditingBuildingActor(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToStopEditing)
{
    auto Pawn = PlayerController->MyFortPawn;
    if (!BuildingActorToStopEditing || !Pawn || BuildingActorToStopEditing->EditingPlayer != (AFortPlayerStateZone*)PlayerController->PlayerState || BuildingActorToStopEditing->bDestroyed)
    {
        return;
    }

    static auto EditToolDef = StaticLoadObject<UFortWeaponItemDefinition>(L"/Game/Items/Weapons/BuildingTools/EditTool.EditTool");

    UFortWorldItem* ToolInstance = nullptr;

    for (auto Instance : PlayerController->WorldInventory->Inventory.ItemInstances)
    {
        if (Instance->ItemEntry.ItemDefinition == EditToolDef)
        {
            ToolInstance = Instance;
            break;
        }
    }

    if (!ToolInstance)
    {
        return;
    }

    Pawn->EquipWeaponDefinition(EditToolDef, ToolInstance->ItemEntry.ItemGuid, ToolInstance->ItemEntry.TrackerGuid, false);

    auto EditTool = (AFortWeap_EditingTool*)(Pawn->CurrentWeapon);

    BuildingActorToStopEditing->EditingPlayer = nullptr;

    if (EditTool)
    {
        EditTool->EditActor = nullptr;
        EditTool->OnRep_EditActor();
    }
}

void Building::ServerBeginEditingBuildingActor(AFortPlayerController* PlayerController, ABuildingSMActor* BuildingActorToEdit)
{
    if (!BuildingActorToEdit || !BuildingActorToEdit->bPlayerPlaced)
    {
        return;
    }

    auto Pawn = PlayerController->MyFortPawn;
    if (!Pawn)
    {
        return;
    }

    auto PlayerState = (AFortPlayerStateZone*)(Pawn->PlayerState);

    if (!PlayerState)
    {
        return;
    }

    if (!BuildingActorToEdit->EditingPlayer)
    {
        BuildingActorToEdit->EditingPlayer = PlayerState;
    }

    static auto EditToolDef = StaticLoadObject<UFortWeaponItemDefinition>(TEXT("/Game/Items/Weapons/BuildingTools/EditTool.EditTool"));

    UFortWorldItem* ToolInstance = nullptr;

    for (auto Instance : PlayerController->WorldInventory->Inventory.ItemInstances)
    {
        if (Instance->ItemEntry.ItemDefinition == EditToolDef)
        {
            ToolInstance = Instance;
            break;
        }
    }

    if (!ToolInstance)
    {
        return;
    }

    Pawn->EquipWeaponDefinition(EditToolDef, ToolInstance->ItemEntry.ItemGuid, ToolInstance->ItemEntry.TrackerGuid, false);

    auto EditTool = (AFortWeap_EditingTool*)(Pawn->CurrentWeapon);

    if (!EditTool)
    {
        return;
    }

    EditTool->EditActor = BuildingActorToEdit;
}

void Building::Hook()
{
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x24D, ServerCreateBuildingActor, nullptr);
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x24F, ServerEditBuildingActor, nullptr);
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x252, ServerEndEditingBuildingActor, nullptr);
    SwapVFTs(AFortPlayerControllerAthena::GetDefaultObj(), 0x254, ServerBeginEditingBuildingActor, nullptr);
}