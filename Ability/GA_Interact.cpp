#include "GA_Interact.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "PCGMap/MapBuild/SectorBuild/Event/PlacedEvent/PlacedInteractActor.h"

UGA_Interact::UGA_Interact()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = GameplayTagSystem::Event_Interact_Portal;
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);

	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Dead);

	// 몽타주 교체 전에 구르기의 루트모션과 상태를 취소 경로로 정리한다.
	CancelAbilitiesWithTag.AddTag(GameplayTagSystem::Ability_Movement_Dodge);

	// 상호작용한 캐릭터만 붙는다. APlayerCharacter 가 이 태그를 보고 이동 입력을 버린다
	ActivationOwnedTags.AddTag(GameplayTagSystem::State_Interacting);

	// 팔을 뻗고 서 있는 동안은 피할 수가 없다. 어빌리티가 끝나면 취소된 경우까지 같이 벗겨진다
	ActivationOwnedTags.AddTag(GameplayTagSystem::State_Invincible);
}

void UGA_Interact::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	InteractTarget = TriggerEventData
		? const_cast<APlacedInteractActor*>(Cast<APlacedInteractActor>(TriggerEventData->OptionalObject.Get()))
		: nullptr;

	if (!InteractTarget.IsValid())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const float FadeDuration = InteractTarget->GetInteractFadeDuration();
	const float HoldTime = InteractTarget->GetInteractHoldTime();

	// 서버에서만 건다. 복제된 Cue 가 모든 클라이언트에서 각자 화면을 덮는다.
	// 페이드 시간은 대상 액터가 단일 출처. bRemoveOnAbilityEnd 라 어빌리티가 끝나면 페이드도 걷힌다
	if (FadeDuration > 0.f && HasAuthority(&ActivationInfo))
	{
		FGameplayCueParameters CueParams;
		CueParams.RawMagnitude = FadeDuration;
		K2_AddGameplayCueWithParams(GameplayTagSystem::GameplayCue_Interact_Portal_Fade, CueParams, true);
	}

	// 태그만으로는 이미 붙은 속도가 안 죽는다. 뛰던 중에 눌러도 그 자리에 선다
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}

	if (UAnimMontage* InteractMontage = InteractTarget->GetInteractMontage())
	{
		// 어빌리티가 끝나는 시점은 화면이 이미 검을 때다. 여기서 같이 끊어야 자세가 안 남는다
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this, NAME_None, InteractMontage, 1.0f, NAME_None, /*bStopWhenAbilityEnds=*/true);
		if (MontageTask)
		{
			// 종료는 시간이 정한다. 몽타주가 먼저 끝나도 연출은 계속 간다
			MontageTask->ReadyForActivation();
		}
	}

	// 화면이 덮일 때까지 + 덮인 채로 버틸 만큼
	UAbilityTask_WaitDelay* DelayTask = UAbilityTask_WaitDelay::WaitDelay(this, FadeDuration + HoldTime);
	if (!DelayTask)
	{
		OnTransitionFinished();
		return;
	}

	DelayTask->OnFinish.AddDynamic(this, &UGA_Interact::OnTransitionFinished);
	DelayTask->ReadyForActivation();
}

void UGA_Interact::OnTransitionFinished()
{
	// 실제 진행은 서버가 결정한다. 클라이언트는 몽타주와 페이드만 본다
	if (HasAuthority(&CurrentActivationInfo) && InteractTarget.IsValid())
	{
		InteractTarget->CompleteInteract();
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
