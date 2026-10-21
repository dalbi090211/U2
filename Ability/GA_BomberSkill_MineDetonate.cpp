#include "GA_BomberSkill_MineDetonate.h"

#include "BomberMine.h"
#include "../Tags/GameplayTagSystem.h"

UGA_BomberSkill_MineDetonate::UGA_BomberSkill_MineDetonate()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	//BP에서 따로 잡지 않아도 이벤트로 뜨게 한다
	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = GameplayTagSystem::Event_Skill_Bomber_MineDetonate;
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);
}

void UGA_BomberSkill_MineDetonate::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ABomberMine* Mine = TriggerEventData
		? Cast<ABomberMine>(const_cast<AActor*>(TriggerEventData->Target.Get()))
		: nullptr;
	if (!Mine)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Mine->Detonate();
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
