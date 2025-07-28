#pragma once
#include "Utils.h"

namespace Misc {
	__int64 __fastcall NetMode(__int64);
	__int64 DispatchRequest(__int64, __int64*, int);
	__int64 SetupAIBotService(UAthenaAIServicePlayerBots*);
	__int64 Omg(UAthenaAIServicePlayerBots*, __int64);

	float GetMaxTickRate();

	bool Context();
	bool HasStreamingLevelsCompletedLoadingUnLoading();
		
	void ProcessEvent(UObject*, UFunction*, void*);
	void ProcessEventClient(UObject*, UFunction*, void*);
	void InitForWorld(UAthenaNavSystem*, UWorld*, EFNavigationSystemRunMode);
	void HandlePostSafeZonePhaseChanged(AFortGameModeAthena*);
	void SetGamePhase(AFortGameStateAthena*, __int64);
	void AdvancePhase(AFortSafeZoneIndicator*);
	void UpdateSafeZonesPhase(AFortGameModeAthena*);
	void EnterAircraft(AFortPlayerControllerAthena*, int64);

	void Hook();
}