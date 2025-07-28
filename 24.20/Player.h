#pragma once
#include "Utils.h"

namespace Player {
	void ServerAcknowledgePossession(AFortPlayerController*, APawn*);
	void ServerLoadingScreenDropped(AFortPlayerControllerAthena*);
	void ServerAttemptAircraftJump(UFortControllerComponent_Aircraft*, FRotator);
	void ServerExecuteInventoryItem(AFortPlayerControllerAthena*, FGuid);
	void EnterAircraft(AFortPlayerControllerAthena*, int64);
	void GetPlayerViewPointAthena(AFortPlayerControllerAthena*, FVector&, FRotator&);
	void ServerAttemptInventoryDrop(AFortPlayerControllerAthena*, FGuid&, int32, bool);
	void ServerHandlePickup(AFortPlayerPawnAthena*, AFortPickup*, FFortPickupRequestInfo);

	void Hook();
}