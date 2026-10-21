#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_BomberSkill_MineDetonate.generated.h"

/*
 * 던져 둔 지뢰를 터뜨린다. Event.Skill.Bomber.MineDetonate로 트리거되고,
 * 터뜨릴 지뢰는 이벤트 페이로드의 Target으로 들어온다.
 *
 * 쿨다운은 던질 때 UGA_BomberSkill_Mine이 이미 걸었고, 지속시간 GE는
 * ABomberMine::Detonate가 걷는다. 몽타주도 입력 GA 쪽에 있다.
 * 이 GA는 서버에서 폭파 신호만 전달한다
 */
UCLASS()
class TEAMPROJECT_FIN_API UGA_BomberSkill_MineDetonate : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_BomberSkill_MineDetonate();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
