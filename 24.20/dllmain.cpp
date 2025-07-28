#include "Utils.h"
#include "Player.h"
#include "Abilities.h"
#include "Building.h"
#include "Client.h"
#include "Misc.h"

DWORD Init(LPVOID)
{
    Sleep(5000);
    AllocConsole();
    FILE* Console;
    freopen_s(&Console, "conout$", "w", stdout);

    Utils::MapName = UKismetStringLibrary::Conv_StringToName(TEXT("Asteria_Terrain"));
    
    MH_Initialize();
    Abilities::Hook();
    Client::SetupPlaylist();
    Building::Hook();
    GameMode::Hook();
    Misc::Hook();
    Replication::Hook();
    Player::Hook();

    MH_EnableHook(0);
    
    Utils::GetFromMemory<bool>(Utils::GetOffset(0x00007FF74AC8FF2C)) = true;
    Utils::GetFromMemory<bool>(Utils::GetOffset(0x00007FF74AA42933)) = false;

    UGameplayStatics::OpenLevel(UWorld::GetWorld(), Utils::MapName, true, TEXT("game=/Game/Athena/Athena_GameMode.Athena_GameMode_C"));

    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), TEXT("net.AllowEncryption 0"), nullptr);
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), TEXT("log LogFortUIDirector off"), nullptr);
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), TEXT("log LogNet off"), nullptr);

    UFortEngine::GetEngine()->GameInstance->LocalPlayers.Free();
    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(0,0,Init,0,0,0);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

