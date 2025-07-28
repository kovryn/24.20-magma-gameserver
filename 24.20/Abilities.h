#pragma once
#include "Utils.h"

namespace Abilities {
	void InternalServerTryActivateAbility(UAbilitySystemComponent*, FGameplayAbilitySpecHandle, bool, FPredictionKey&, FGameplayEventData*);
	inline FGameplayAbilitySpec* FindAbilityFromSpecHandle(UAbilitySystemComponent*, FGameplayAbilitySpecHandle&);
	void Hook();
}