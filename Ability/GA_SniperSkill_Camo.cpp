#include "GA_SniperSkill_Camo.h"

#include "../Tags/GameplayTagSystem.h"
#include "../Character/PlayerCharacter.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UGA_SniperSkill_Camo::UGA_SniperSkill_Camo()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> Sound(TEXT("/Game/TPSDevelop/Class/Skill/Sounds/S_CamoWhoosh"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Enter(TEXT("/Game/TPSDevelop/Class/Skill/Sounds/S_CamoEnter"));
	EnterSound = Enter.Object;
	ExitSound = Sound.Object;
	//상태 관리와 GE 적용이 전부 서버 몫이라 예측할 것이 없다
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	//장막은 스킬이 도는 동안 유지된다. GAS가 EndAbility에서 알아서 뗀다
	ActivationOwnedTags.AddTag(GameplayTagSystem::State_Skill_Camo_Veil);

	//장막 중 재시전 방지
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Skill_Camo_Veil);
}

void UGA_SniperSkill_Camo::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	//OnlyTriggerOnce = false. 장막이 끝날 때까지 매 발사를 듣는다
	FiredEventTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this,
		GameplayTagSystem::Event_Weapon_Fired,
		nullptr,
		false,
		true);

	if (IsValid(FiredEventTask))
	{
		FiredEventTask->EventReceived.AddDynamic(this, &UGA_SniperSkill_Camo::OnWeaponFired);
		FiredEventTask->ReadyForActivation();
	}

	ApplyVeilEffect();

	World->GetTimerManager().SetTimer(
		VeilTimerHandle,
		this,
		&UGA_SniperSkill_Camo::OnVeilExpired,
		VeilDuration,
		false);

	//시전 직후에는 2초를 기다리지 않는다
	EnterStealth();
}

void UGA_SniperSkill_Camo::OnWeaponFired(FGameplayEventData Payload)
{

	//은신 중이었다면 여기서 강화가 소비된다.
	//GA_Fire는 데미지를 적용한 뒤에 이 이벤트를 쏘므로(GA_Fire.cpp:84, :125) 그 한 발은 강화가 적용된 상태다
	ExitStealth();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	//공격할 때마다 재진입 창이 처음부터 다시 시작된다
	World->GetTimerManager().SetTimer(
		StealthTimerHandle,
		this,
		&UGA_SniperSkill_Camo::EnterStealth,
		StealthDelay,
		false);
}

void UGA_SniperSkill_Camo::ApplyVeilEffect()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!VeilEffectClass || !ASC)
	{
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle =
		MakeOutgoingGameplayEffectSpec(VeilEffectClass, GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		return;
	}

	//지속 시간의 출처는 VeilDuration 하나. GE 에셋 값과 타이머가 어긋나지 않게 덮어쓴다
	SpecHandle.Data->SetDuration(VeilDuration, true);

	VeilHandle = ApplyGameplayEffectSpecToOwner(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		SpecHandle);
}

void UGA_SniperSkill_Camo::EnterStealth()
{
	if (EmpowerHandle.IsValid())
	{
		return;
	}

	if (!EmpowerEffectClass)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle =
		MakeOutgoingGameplayEffectSpec(EmpowerEffectClass, GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		return;
	}

	EmpowerHandle = ApplyGameplayEffectSpecToOwner(
		CurrentSpecHandle,
		CurrentActorInfo,
		CurrentActivationInfo,
		SpecHandle);

	ASC->AddLooseGameplayTag(GameplayTagSystem::State_Skill_Camo_Stealth);
	if (EmpowerHandle.IsValid())
	{
		CastChecked<APlayerCharacter>(GetAvatarActorFromActorInfo())->ClientPlayCamoSound(EnterSound);
	}
}

void UGA_SniperSkill_Camo::ExitStealth()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	if (EmpowerHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(EmpowerHandle);
		EmpowerHandle.Invalidate();
		CastChecked<APlayerCharacter>(GetAvatarActorFromActorInfo())->ClientPlayCamoSound(ExitSound);
	}

	ASC->RemoveLooseGameplayTag(GameplayTagSystem::State_Skill_Camo_Stealth);
}

void UGA_SniperSkill_Camo::OnVeilExpired()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UGA_SniperSkill_Camo::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	//장막이 끝나면 은신도 같이 끝난다. 취소/사망으로 들어와도 GE가 남지 않아야 한다
	ExitStealth();

	//20초를 채우고 끝나면 GE도 같이 만료되지만, 취소/사망으로 일찍 끝나면 남는다
	if (VeilHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveActiveGameplayEffect(VeilHandle);
		}
		VeilHandle.Invalidate();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VeilTimerHandle);
		World->GetTimerManager().ClearTimer(StealthTimerHandle);
	}

	FiredEventTask = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
