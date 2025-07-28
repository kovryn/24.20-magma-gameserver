#pragma once
#include "Utils.h"

namespace Replication
{
	inline bool bSkipServerReplicateActors = false;

	inline void (*TickFlushOG)(SDK::UNetDriver* Driver, float a2);
	
	void FlushNetDormancy(SDK::AActor* Actor);
	void SetNetDormancy(SDK::AActor* Actor, SDK::ENetDormancy NewDormancy);

	void TickFlush(SDK::UNetDriver* Driver, float a2);
	void Hook();
}