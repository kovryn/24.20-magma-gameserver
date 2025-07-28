#pragma once
#include "Utils.h"

static inline __int8 LastTeam = 3;

namespace GameMode
{
	inline bool (*ReadyToStartMatchOG)(SDK::AFortGameModeAthena*);

	inline bool ReadyToStartMatch(SDK::AFortGameModeAthena* GameMode);
	inline SDK::APawn* SpawnDefaultPawnFor(SDK::AFortGameModeAthena* GameMode, SDK::AController* NewPlayer, SDK::AActor* StartSpot);
	inline __int8 PickTeam();

	inline void HandleStartingNewPlayer(SDK::AFortGameModeAthena* GameMode, SDK::AFortPlayerControllerAthena* NewPlayer);
	 void Hook();
}
