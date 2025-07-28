#include "Abilities.h"
using namespace Utils;

inline FGameplayAbilitySpec* Abilities::FindAbilityFromSpecHandle(UAbilitySystemComponent* AbilitySystemComponent, FGameplayAbilitySpecHandle& SpecHandle)
{
    for (int i = 0; i < AbilitySystemComponent->ActivatableAbilities.Items.Num(); i++)
    {
        if (AbilitySystemComponent->ActivatableAbilities.Items[i].Handle.Handle == SpecHandle.Handle)
        {
            return &AbilitySystemComponent->ActivatableAbilities.Items[i];
        }
    }

    return nullptr;
}


void Abilities::InternalServerTryActivateAbility(UAbilitySystemComponent* AbilitySystemComponent, FGameplayAbilitySpecHandle Handle, bool InputPressed, FPredictionKey& PredictionKey, FGameplayEventData* TriggerEventData)
{
    auto Spec = FindAbilityFromSpecHandle(AbilitySystemComponent, Handle);
    if (!Spec) return AbilitySystemComponent->ClientActivateAbilityFailed(Handle, PredictionKey.Current);

    UGameplayAbility* AbilityToActivate = Spec->Ability;
    UGameplayAbility* InstancedAbility = nullptr;

    Spec->InputPressed = true;

    static bool (*InternalTryActivateAbility)(UAbilitySystemComponent * AbilitySystemComp, FGameplayAbilitySpecHandle AbilityToActivate, FPredictionKey InPredictionKey, UGameplayAbility * *OutInstancedAbility, void* OnGameplayAbilityEndedDelegate, const FGameplayEventData * TriggerEventData) = decltype(InternalTryActivateAbility)(Utils::GetAddr(0x00007FF742ABDF4C));
    if (!InternalTryActivateAbility(AbilitySystemComponent, Handle, PredictionKey, &InstancedAbility, nullptr, TriggerEventData))
    {
        AbilitySystemComponent->ClientActivateAbilityFailed(Handle, PredictionKey.Current);
        Spec->InputPressed = false;
        AbilitySystemComponent->ActivatableAbilities.MarkItemDirty((*Spec));
    }
}

void Abilities::Hook()
{
    SwapVFTs(UAbilitySystemComponent::GetDefaultObj(), 0x115, InternalServerTryActivateAbility, nullptr);
    SwapVFTs(UFortAbilitySystemComponent::GetDefaultObj(), 0x115, InternalServerTryActivateAbility, nullptr);
    SwapVFTs(UFortAbilitySystemComponentAthena::GetDefaultObj(), 0x115, InternalServerTryActivateAbility, nullptr);
}