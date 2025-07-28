#include "Client.h"
using namespace Utils;

void Client::SetupPlaylist() {
    while (!UFortEngine::GetEngine() || !UWorld::GetWorld()) Sleep(1500);
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), TEXT("net.AllowEncryption 0"), nullptr);
    UFortEngine::GetEngine()->GameViewport->ViewportConsole = (UFortConsole*)UGameplayStatics::SpawnObject(UFortConsole::StaticClass(), UFortEngine::GetEngine()->GameViewport);

    SwapInstruction(Utils::GetAddr(0x00007FF743F01901), 1, 00);

    auto Playlist = UObject::FindObject<UFortPlaylistAthena>("FortPlaylistAthena Playlist_ShowdownAlt_Solo.Playlist_ShowdownAlt_Solo");
    while (!Playlist) Playlist = UObject::FindObject<UFortPlaylistAthena>("FortPlaylistAthena Playlist_ShowdownAlt_Solo.Playlist_ShowdownAlt_Solo");

    FUIExtension Extension{};
    Extension.Slot = EUIExtensionSlot::Primary;
    Extension.WidgetClass.ObjectID.AssetPath.AssetName = UKismetStringLibrary::Conv_StringToName(TEXT("ArenaScoringHUD_C"));
    Extension.WidgetClass.ObjectID.AssetPath.PackageName = UKismetStringLibrary::Conv_StringToName(TEXT("/Game/UI/Competitive/Arena/ArenaScoringHUD"));

    Playlist->UIExtensions.Add(Extension);

    Playlist->bRespawnInAir = true;
    Playlist->bForceRespawnLocationInsideOfVolume = true;
    SetScalableFloatVal(Playlist->RespawnHeight, 15000);
    SetScalableFloatVal(Playlist->RespawnTime, 5);
    Playlist->RespawnType = EAthenaRespawnType::InfiniteRespawn;
}