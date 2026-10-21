#pragma once

#include "CoreMinimal.h"
#include "GA_WeaponAbilityBase.h"
#include "GA_Fire.generated.h"

class UGameplayEffect;
class UAnimMontage;
class UWeaponComponent;
class UAbilitySystemComponent;

UCLASS()
class TEAMPROJECT_FIN_API UGA_Fire : public UGA_WeaponAbilityBase
{
	GENERATED_BODY()

public:
	UGA_Fire();
	TSubclassOf<UGameplayEffect> GetDamageEffectClass() const { return DamageEffectClass; }
	UAnimMontage* GetFireMontage() const { return FireMontage; }

protected:
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnRemoveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fire")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fire|Animation")
	TObjectPtr<UAnimMontage> FireMontage;

private:
	bool TryFireOnce();
	bool TryActivateReloadIfEmpty(const UWeaponComponent* WeaponComponent, UAbilitySystemComponent* ASC) const;
	void ScheduleNextFire();
	void HandleFireTimer();
	float GetFireInterval(const FGameplayAbilityActorInfo* ActorInfo) const;

	FTimerHandle FireTimerHandle;
};
