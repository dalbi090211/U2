#include "GA_Fire.h"

#include "../Tags/GameplayTagSystem.h"
#include "../Weapon/Core/WeaponBase.h"
#include "../Weapon/Core/WeaponComponent.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

UGA_Fire::UGA_Fire()
{
	FGameplayTagContainer DefaultAbilityTags;
	DefaultAbilityTags.AddTag(GameplayTagSystem::Ability_Weapon_Fire);
	SetAssetTags(DefaultAbilityTags);
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Weapon_Reloading);
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Weapon_SmartTracking);
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Weapon_SmartTrackingVolley);
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Movement_Dodging);
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Skill_Bomber_MineThrowing);
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Movement_Sprinting);
	ActivationBlockedTags.AddTag(GameplayTagSystem::State_Dead);
}

bool UGA_Fire::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	const bool bCanActivate = Super::CanActivateAbility(
		Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
	if (!bCanActivate && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("[Fire] Failed: Ability activation blocked"));
	}
	return bCanActivate;
}

void UGA_Fire::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Clients forward input; only confirmed server shots produce feedback.
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("[Fire] Failed: CommitAbility"));
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!TryFireOnce())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ScheduleNextFire();
}

void UGA_Fire::InputReleased(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGA_Fire::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimerHandle);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UGA_Fire::TryFireOnce()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromInfo(ActorInfo);
	UWeaponComponent* WeaponComponent = FindAvatarWeaponComponent(ActorInfo);
	if (!WeaponComponent || TryActivateReloadIfEmpty(WeaponComponent, ASC))
	{
		return false;
	}

	const bool bFired = WeaponComponent->ExecuteFire(
		ASC, DamageEffectClass, GetAbilityLevel(GetCurrentAbilitySpecHandle(), ActorInfo));
	if (bFired)
	{
		BroadcastSelfGameplayEvent(ASC, GetAvatarActor(ActorInfo), GameplayTagSystem::Event_Weapon_Fired);
	}
	return bFired;
}

bool UGA_Fire::TryActivateReloadIfEmpty(const UWeaponComponent* WeaponComponent, UAbilitySystemComponent* ASC) const
{
	const AWeaponBase* Weapon = WeaponComponent ? WeaponComponent->GetEquippedWeapon() : nullptr;
	if (!Weapon || !ASC || Weapon->GetCurrentAmmo() > 0 || !WeaponComponent->CanReload())
	{
		return false;
	}

	FGameplayTagContainer ReloadTags;
	ReloadTags.AddTag(GameplayTagSystem::Ability_Weapon_Reload);
	return ASC->TryActivateAbilitiesByTag(ReloadTags);
}

void UGA_Fire::ScheduleNextFire()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	const float FireInterval = GetFireInterval(GetCurrentActorInfo());
	World->GetTimerManager().SetTimer(
		FireTimerHandle,
		this,
		&UGA_Fire::HandleFireTimer,
		FireInterval,
		false
	);
}

void UGA_Fire::HandleFireTimer()
{
	if (!TryFireOnce())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	ScheduleNextFire();
}

float UGA_Fire::GetFireInterval(const FGameplayAbilityActorInfo* ActorInfo) const
{
	const UWeaponComponent* WeaponComponent = FindAvatarWeaponComponent(ActorInfo);
	const AWeaponBase* Weapon = WeaponComponent ? WeaponComponent->GetEquippedWeapon() : nullptr;
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromInfo(ActorInfo);
	return Weapon ? Weapon->GetCurrentFireInterval(ASC) : 0.1f;
}

void UGA_Fire::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	if (UWeaponComponent* Component = FindAvatarWeaponComponent(ActorInfo))
	{
		Component->RegisterFireDamageEffect(Spec.Handle, DamageEffectClass);
	}
}

void UGA_Fire::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);
	if (UWeaponComponent* Component = FindAvatarWeaponComponent(ActorInfo))
	{
		Component->RegisterFireDamageEffect(Spec.Handle, DamageEffectClass);
	}
}

void UGA_Fire::OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	if (UWeaponComponent* Component = FindAvatarWeaponComponent(ActorInfo))
	{
		Component->UnregisterFireDamageEffect(Spec.Handle);
	}
	Super::OnRemoveAbility(ActorInfo, Spec);
}
