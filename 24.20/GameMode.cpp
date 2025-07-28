#include "GameMode.h"
#include <fstream>

using namespace std;
using namespace SDK;

#include "minhook/MinHook.h"
#include "Options.h"
using namespace Utils;
#pragma comment(lib, "minhook/minhook.lib")

static UNetDriver* (*CreateNetDriver)
(
    UEngine* Engine,
    int64 InWorld,
    FName NetDriverDefinition,
    int a4
    ) = decltype(CreateNetDriver)(InSDKUtils::GetImageBase() + 0x2429AF8);

static bool (*InitListen)
(
    UNetDriver* Driver,
    void* InNotify,
    FURL& ListenURL,
    bool bReuseAddressAndPort,
    FString& Error
    ) = decltype(InitListen)(InSDKUtils::GetImageBase() + 0x87E0A68);

static void(*SetWorld)(void*, void*) = decltype(SetWorld)(InSDKUtils::GetImageBase() + 0x16F650C);

bool GameMode::ReadyToStartMatch(AFortGameModeAthena* GameMode)
{
    auto World = UWorld::GetWorld();

    if (!World || World->Name.ComparisonIndex != Utils::MapName.ComparisonIndex)
        return ReadyToStartMatchOG(GameMode);

    auto GameState = (AFortGameStateAthena*)GameMode->GameState;

    if (!GameState->CurrentPlaylistInfo.BasePlaylist)
    {
        GameState->CurrentPlaylistInfo.BasePlaylist = UObject::FindObject<UFortPlaylistAthena>("FortPlaylistAthena Playlist_ShowdownAlt_Solo.Playlist_ShowdownAlt_Solo");
        GameState->CurrentPlaylistInfo.BasePlaylist->SafeZoneStartUp = ESafeZoneStartUp::StartsWithAirCraft;
        GameState->CurrentPlaylistInfo.PlaylistReplicationKey++;
        GameState->CurrentPlaylistInfo.BasePlaylist->GarbageCollectionFrequency = 9999999999999999;

        SetScalableFloatVal(GameMode->PlaylistEnableBots, 1);

        auto Playlist = GameState->CurrentPlaylistInfo.BasePlaylist;

        if (bLateGame)
        {
            Playlist->bRespawnInAir = true;
            Playlist->bForceRespawnLocationInsideOfVolume = true;
            SetScalableFloatVal(Playlist->RespawnHeight, 15000);
            SetScalableFloatVal(Playlist->RespawnTime, 5);
            Playlist->RespawnType = EAthenaRespawnType::InfiniteRespawn;
            GameState->bCheatRespawnEnabled = true;
            GameMode->MinRespawnDelay = 5;
            Playlist->DBNOType = EDBNOType::Off;
            Playlist->bAllowJoinInProgress = true;
            Playlist->JoinInProgressMatchType = UKismetTextLibrary::Conv_StringToText(TEXT("Creative"));
        }

        GameMode->AISettings = LoadSoftObjectPtr<UAthenaAISettings>(GameState->CurrentPlaylistInfo.BasePlaylist->AISettings);

        if (GameMode->AISettings)
        {
            if (!GameMode->SpawningPolicyManager)
            {
                GameMode->SpawningPolicyManager = SpawnActor<AFortAthenaSpawningPolicyManager>({}, {});
            }

            GameMode->SpawningPolicyManager->GameModeAthena = GameMode;
            GameMode->SpawningPolicyManager->GameStateAthena = GameState;

            for (size_t i = 0; i < GameMode->AISettings->AIServices.Num(); i++)
            {
                auto Service = GameMode->AISettings->AIServices[i];
                if (!Service.ClassPtr)
                {
                    //printf("YAY\n");
                    GameMode->AISettings->AIServices.Remove(i);
                    GameMode->AISettings->AIServices.Add(UAthenaAIServicePlayerBots::StaticClass());
                    break;
                }
            }
        }

        GameMode->bAlwaysDBNO = false;
        GameMode->bDBNOEnabled = false;
        GameState->SetIsDBNODeathEnabled(false);
        GameState->bDBNODeathEnabled = false;
        GameState->bDBNOEnabledForGameMode = false;
    }

    if (!GameState || !GameState->MapInfo)
        return ReadyToStartMatchOG(GameMode);

    if (!GameMode->bWorldIsReady)
    {
        if (!LightAmmo)
            LightAmmo = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsLight.AthenaAmmoDataBulletsLight");
        if (!MediumAmmo)
            MediumAmmo = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsMedium.AthenaAmmoDataBulletsMedium");
        if (!HeavyAmmo)
            HeavyAmmo = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsHeavy.AthenaAmmoDataBulletsHeavy");
        if (!Shells)
            Shells = StaticLoadObject<UFortItemDefinition>(L"/Game/Athena/Items/Ammo/AthenaAmmoDataShells.AthenaAmmoDataShells");

        if (!Wood)
            Wood = StaticLoadObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/WoodItemData.WoodItemData");
        if (!Stone)
            Stone = StaticLoadObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/StoneItemData.StoneItemData");
        if (!Metal)
            Metal = StaticLoadObject<UFortItemDefinition>(L"/Game/Items/ResourcePickups/MetalItemData.MetalItemData");

        GameMode->bWorldIsReady = true;

        GameState->bIsUsingDownloadOnDemand = false;
        //GameState->bPlaylistDataIsLoaded = true;

        GameState->AirCraftBehavior = GameState->CurrentPlaylistInfo.BasePlaylist->AirCraftBehavior;
        GameState->CachedSafeZoneStartUp = GameState->CurrentPlaylistInfo.BasePlaylist->SafeZoneStartUp;

        GameState->OnRep_CurrentPlaylistInfo();
        //AFortPlayerControllerAthena::bEnableBroadcastRemoteClientInfo
        GameMode->bDisableGCOnServerDuringMatch = true;
        GameMode->bPlaylistHotfixChangedGCDisabling = true;

        static __int64 (*real)(__int64, __int64) = decltype(real)(InSDKUtils::GetImageBase() + 0xDAE464);
        auto context = real(int64(UEngine::GetEngine()), int64(UWorld::GetWorld()));
        FName GameNetDriver = UKismetStringLibrary::Conv_StringToName(TEXT("GameNetDriver"));
        auto Driver = CreateNetDriver(UEngine::GetEngine(), context, GameNetDriver, 0);
        Driver->NetDriverName = GameNetDriver;
        Driver->World = UWorld::GetWorld();

        GameMode->GameSession->MaxPlayers = 100;

        ofstream weap = ofstream("Weapons.txt");

        for (int i = 0; i < UObject::GObjects->Num(); i++)
        {
            UFortWorldItemDefinition* Object = (UFortWorldItemDefinition*)UObject::GObjects->GetByIndex(i);

            if (!Object || Object->IsDefaultObject() || (!Object->IsA(UFortWeaponItemDefinition::StaticClass()) && !Object->IsA(UFortConsumableItemDefinition::StaticClass()) && !Object->IsA(UFortResourceItemDefinition::StaticClass())))
                continue;

            FString DispName = UKismetTextLibrary::Conv_TextToString(Object->DisplayName);
            if (!DispName.IsValid())
                continue;

            weap << DispName.ToString() << " " << UKismetSystemLibrary::GetPathName(Object).ToString() << endl;
        }

        weap.close();

        for (size_t i = 0; i < UObject::GObjects->Num(); i++)
        {
            auto Obj = UObject::GObjects->GetByIndex(i);
            if (!Obj || Obj->IsDefaultObject() || !Obj->IsA(UFortWeaponRangedItemDefinition::StaticClass()))
                continue;
            auto Def = (UFortWeaponRangedItemDefinition*)Obj;
            auto AmmoData = Def->GetAmmoWorldItemDefinition_BP();
            if (AmmoData && AmmoData != Def && (AmmoData == Shells || AmmoData == HeavyAmmo))
            {
                if (!Def->WeaponStatHandle.DataTable || !Def->WeaponStatHandle.RowName.ComparisonIndex)
                    return 0;

                auto DataTable = Def->WeaponStatHandle.DataTable;
                FName Name = Def->WeaponStatHandle.RowName;

                TMap<FName, FFortRangedWeaponStats*>& WeaponStatTable = *(TMap<FName, FFortRangedWeaponStats*>*)(int64(DataTable) + 0x30);

                for (auto& Pair : WeaponStatTable)
                {
                    if (Pair.Key().ComparisonIndex == Name.ComparisonIndex)
                    {
                        Pair.Value()->KnockbackMagnitude = 0;
                        Pair.Value()->KnockbackZAngle = 0;
                        Pair.Value()->LongRangeKnockbackMagnitude = 0;
                        Pair.Value()->MidRangeKnockbackMagnitude = 0;
                        break;
                    }
                }
            }
        }

        FString Error;
        FURL InURL = FURL();
        InURL.Port = 7777;

        GameState->OnFinishedStreamingAdditionalPlaylistLevel();
        GameState->OnRep_AdditionalPlaylistLevelsStreamed();

        InitListen(Driver, UWorld::GetWorld(), InURL, false, Error);
        SetWorld(Driver, UWorld::GetWorld());

        SetConsoleTitleA("Listening");
        //printf("Listening\n");

        UWorld::GetWorld()->NetDriver = Driver;
        UWorld::GetWorld()->LevelCollections[0].NetDriver = Driver;
        UWorld::GetWorld()->LevelCollections[1].NetDriver = Driver;
    }

    return GameMode->AlivePlayers.Num();
}

APawn* GameMode::SpawnDefaultPawnFor(SDK::AFortGameModeAthena* GameMode, SDK::AController* NewPlayer, SDK::AActor* StartSpot)
{
    if (!StartSpot)
    {
        auto transform = StartSpot->GetTransform();
        transform.Scale3D = FVector{ 1,1,1 };
        transform.Translation = { 0,0,1000 };
        return GameMode->SpawnDefaultPawnAtTransform(NewPlayer, transform);
    }

    auto transform = StartSpot->GetTransform();
    transform.Scale3D = FVector{ 1,1,1 };
    if (((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->GamePhase >= EAthenaGamePhase::SafeZones && StartSpot)
    {
        transform.Translation = ((AFortGameStateAthena*)UWorld::GetWorld()->GameState)->SafeZoneIndicator->GetSafeZoneCenter() + UKismetMathLibrary::RandomFloatInRange(-2500, 2500);
        transform.Translation.Z = 15000;
    }
    auto ret = GameMode->SpawnDefaultPawnAtTransform(NewPlayer, transform);
    if (!ret)
    {
        transform.Scale3D = FVector{ 1,1,1 };
        transform.Translation = { 0,0,1000 };
        return GameMode->SpawnDefaultPawnAtTransform(NewPlayer, transform);
    }
    return ret;
}

void (*HandleStartingNewPlayerOG)(AFortGameModeAthena* GameMode, AFortPlayerControllerAthena* Controller);
void GameMode::HandleStartingNewPlayer(AFortGameModeAthena* GameMode, AFortPlayerControllerAthena* Controller)
{
    static bool First = false;

    if (((AFortGameStateAthena*)GameMode->GameState)->GamePhase == EAthenaGamePhase::Warmup && GameMode->AlivePlayers.Num() > 0)
        ((AFortGameStateAthena*)GameMode->GameState)->WarmupCountdownEndTime += 15;

    ((AFortPlayerStateAthena*)Controller->PlayerState)->WorldPlayerId = Controller->PlayerState->GetPlayerId();

    if (!First)
    {
        First = true;
        auto World = UWorld::GetWorld();

        for (auto StreamingLevel : World->StreamingLevels)
        {
            if (!StreamingLevel)
                continue;
            StreamingLevel->SetIsRequestingUnloadAndRemoval(false);
            StreamingLevel->SetShouldBeLoaded(true);
            StreamingLevel->SetShouldBeVisible(true);
        }
    }

    return HandleStartingNewPlayerOG(GameMode, Controller);
}

__int8 GameMode::PickTeam()
{
    auto Ret = LastTeam;
    LastTeam++;
    return Ret;
}

void GameMode::Hook()
{
    SwapVFTs(AFortGameModeBR::GetDefaultObj(), 0xE8, HandleStartingNewPlayer, (void**)&HandleStartingNewPlayerOG);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF743E532F4)), PickTeam, nullptr);
    SwapVFTs(AFortGameModeBR::GetDefaultObj(), 0xE2, GameMode::SpawnDefaultPawnFor, nullptr);
    MH_CreateHook((void*)(Utils::GetAddr(0x00007FF743E54564)), GameMode::ReadyToStartMatch, (void**)&ReadyToStartMatchOG);
}
