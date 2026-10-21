#include "GA_BomberSkill_Mine.h"

#include "BomberMine.h"
#include "../Tags/GameplayTagSystem.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

void UGA_BomberSkill_Mine::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!AvatarActor || !ASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	//두 번째 입력. 폭파는 전용 GA에 맡기고 여기서는 연출만 낸다
	if (ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Skill_Bomber_MineArmed))
	{
		if (ActorInfo->IsNetAuthority())
		{
			SendDetonateEvent(AvatarActor);
			ActiveMine = nullptr;
		}

		PlaySkillMontage(ActorInfo, ActivationInfo, DetonateMontage);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const float MontageSeconds = PlaySkillMontage(ActorInfo, ActivationInfo, ThrowMontage);

	//손을 떠나는 프레임의 노티파이를 기다린다. 실제 스폰은 ReleaseMine 한 곳에서만 한다
	UAbilityTask_WaitGameplayEvent* WaitThrowNotify = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		GameplayTagSystem::Event_Skill_Bomber_MineThrow,
		nullptr,
		true);
	WaitThrowNotify->EventReceived.AddDynamic(this, &UGA_BomberSkill_Mine::OnThrowNotify);
	WaitThrowNotify->ReadyForActivation();

	//몽타주가 없거나 노티파이가 안 박혀 있어도 지뢰는 나가야 한다
	if (MontageSeconds <= 0.0f)
	{
		ReleaseMine();
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		ThrowFallbackTimerHandle,
		this,
		&UGA_BomberSkill_Mine::ReleaseMine,
		MontageSeconds,
		false);
}

void UGA_BomberSkill_Mine::OnThrowNotify(FGameplayEventData Payload)
{
	ReleaseMine();
}

void UGA_BomberSkill_Mine::ReleaseMine()
{
	//노티파이가 먼저 오면 예비 타이머는 필요 없다
	GetWorld()->GetTimerManager().ClearTimer(ThrowFallbackTimerHandle);

	const FGameplayAbilitySpecHandle Handle = GetCurrentAbilitySpecHandle();
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const FGameplayAbilityActivationInfo ActivationInfo = GetCurrentActivationInfo();

	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!AvatarActor)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	//스폰은 서버만. 클라이언트가 예측으로 지뢰를 만들면 서버 것과 두 개가 보인다
	if (ActorInfo->IsNetAuthority())
	{
		ThrowMine(AvatarActor);
		if (!ActiveMine.IsValid())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	//GE는 예측으로 건다. 클라이언트가 바로 태그를 봐야 다음 입력이 폭파로 간다
	ApplyArmedEffect(Handle, ActorInfo, ActivationInfo);

	//지뢰가 손을 떠났으니 기본공격을 푼다. 몽타주 남은 구간은 막지 않는다
	if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
	{
		ASC->SetLooseGameplayTagCount(GameplayTagSystem::State_Skill_Bomber_MineThrowing, 0);
	}

	//여기서 끝나야 State.Skill.Active가 풀리고 다음 입력이 폭파로 간다
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

bool UGA_BomberSkill_Mine::CheckCooldown(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	//지뢰가 남아 있으면 쿨다운을 무시한다. 두 번째 입력이 폭파로 가야 한다
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (ASC && ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Skill_Bomber_MineArmed))
	{
		return true;
	}

	return Super::CheckCooldown(Handle, ActorInfo, OptionalRelevantTags);
}

void UGA_BomberSkill_Mine::ThrowMine(AActor* AvatarActor)
{
	UWorld* World = GetWorld();
	if (!MineClass || !World)
	{
		return;
	}

	const APawn* AvatarPawn = Cast<APawn>(AvatarActor);
	const FRotator AimRotation = AvatarPawn
		? AvatarPawn->GetBaseAimRotation()
		: AvatarActor->GetActorRotation();

	const FVector SpawnLocation = AvatarActor->GetActorLocation()
		+ AimRotation.Vector() * ThrowForwardOffset
		+ FVector::UpVector * ThrowHeightOffset;

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = AvatarActor;
	SpawnParameters.Instigator = Cast<APawn>(AvatarActor);
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ABomberMine* Mine = World->SpawnActor<ABomberMine>(
		MineClass,
		SpawnLocation,
		AimRotation,
		SpawnParameters);
	if (!Mine)
	{
		return;
	}

	const FRotator ThrowRotation(AimRotation.Pitch + ThrowPitchOffset, AimRotation.Yaw, AimRotation.Roll);
	Mine->Throw(
		AvatarActor,
		GetAbilitySystemComponentFromActorInfo(),
		ThrowRotation.Vector() * ThrowSpeed,
		GetAbilityLevel(),
		ArmedSeconds);

	ActiveMine = Mine;
}

void UGA_BomberSkill_Mine::ApplyArmedEffect(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	if (!ArmedEffectClass)
	{
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(
		Handle,
		ActorInfo,
		ActivationInfo,
		ArmedEffectClass,
		GetAbilityLevel(Handle, ActorInfo));
	if (!SpecHandle.IsValid())
	{
		return;
	}

	//지속 시간의 출처는 ArmedSeconds 하나. GE 에셋 값과 신관이 어긋나지 않게 덮어쓴다
	SpecHandle.Data->SetDuration(ArmedSeconds, true);
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}

void UGA_BomberSkill_Mine::SendDetonateEvent(AActor* AvatarActor)
{
	FGameplayEventData EventData;
	EventData.EventTag = GameplayTagSystem::Event_Skill_Bomber_MineDetonate;
	EventData.Instigator = AvatarActor;
	EventData.Target = ActiveMine.Get();

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		AvatarActor,
		EventData.EventTag,
		EventData);
}

float UGA_BomberSkill_Mine::PlaySkillMontage(
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	UAnimMontage* Montage)
{
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!Montage || !ASC)
	{
		return 0.0f;
	}

	//기다리지 않는다. 어빌리티가 끝나도 재생은 이어진다
	const float MontageLength = ASC->PlayMontage(this, ActivationInfo, Montage, MontagePlayRate);
	if (MontageLength <= 0.0f)
	{
		return 0.0f;
	}

	//재생 중에는 기본공격을 막는다. 어빌리티가 이미 끝나 ActivationOwnedTags를 쓸 수 없어
	//태그를 직접 걸고 몽타주 길이만큼 뒤에 걷는다. 두 번 겹쳐도 카운트가 새지 않게 개수를 지정한다
	ASC->SetLooseGameplayTagCount(GameplayTagSystem::State_Skill_Bomber_MineThrowing, 1);

	//이미 사격 중이었으면 활성화 차단만으로는 멈추지 않는다
	FGameplayTagContainer FireAbilityTags(GameplayTagSystem::Ability_Weapon_Fire);
	ASC->CancelAbilities(&FireAbilityTags);

	const float MontageSeconds = MontageLength / FMath::Max(MontagePlayRate, UE_KINDA_SMALL_NUMBER);
	GetWorld()->GetTimerManager().SetTimer(
		MontageTagTimerHandle,
		[WeakASC = TWeakObjectPtr<UAbilitySystemComponent>(ASC)]()
		{
			if (WeakASC.IsValid())
			{
				WeakASC->SetLooseGameplayTagCount(GameplayTagSystem::State_Skill_Bomber_MineThrowing, 0);
			}
		},
		MontageSeconds,
		false);

	return MontageSeconds;
}
