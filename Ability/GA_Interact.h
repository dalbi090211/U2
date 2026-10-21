#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Interact.generated.h"

class APlacedInteractActor;

/*
 * 상호작용 연출. Event.Interact.Portal 로 발동한다.
 *
 * 몽타주   ASC 가 모든 머신에 뿌린다. 옆 사람에게도 보인다
 * 페이드   GameplayCue.Interact.Portal.Fade 가 각 머신에서 로컬 화면을 덮는다
 * 완료     FadeDuration + HoldTime 이 지나면 서버가 대상의 CompleteInteract 를 부른다
 *
 * 연출 값은 전부 상호작용한 액터가 들고 있다. 종료 시점은 몽타주가 아니라 시간이 정한다
 */
UCLASS()
class TEAMPROJECT_FIN_API UGA_Interact : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Interact();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

private:
	UFUNCTION()
	void OnTransitionFinished();

	// 연출이 끝난 뒤에 실행해야 하므로 발동 시점의 대상을 들고 있는다
	TWeakObjectPtr<APlacedInteractActor> InteractTarget;
};
