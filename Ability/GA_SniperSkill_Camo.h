#pragma once

#include "CoreMinimal.h"
#include "GA_CharacterSkillBase.h"
#include "GA_SniperSkill_Camo.generated.h"

class UAbilityTask_WaitGameplayEvent;
class UGameplayEffect;
class USoundBase;

/*
 * 장막(Veil) 20초. 그 안에서 은신(Stealth)이 붙었다 떨어졌다 한다.
 *
 *   시전            → 장막 ON + 은신 즉시 ON
 *   공격            → 은신 OFF (강화 1발 소비) + 재진입 타이머 2초 시작
 *   2초 무공격      → 은신 ON
 *   20초 경과       → 장막 OFF, 은신 OFF, 스킬 종료
 *
 * 은신 중 EmpowerEffectClass가 적용되어 다음 한 발이 강화된다.
 */
UCLASS()
class TEAMPROJECT_FIN_API UGA_SniperSkill_Camo : public UGA_CharacterSkillBase
{
	GENERATED_BODY()

public:
	UGA_SniperSkill_Camo();

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camo|Sound")
	TObjectPtr<USoundBase> EnterSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camo|Sound")
	TObjectPtr<USoundBase> ExitSound;

	//장막 지속 시간
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camo", meta = (ClampMin = "0.1"))
	float VeilDuration = 20.0f;

	//마지막 공격 이후 이 시간만큼 공격이 없으면 은신에 다시 들어간다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camo", meta = (ClampMin = "0.0"))
	float StealthDelay = 2.0f;

	//은신 중에만 걸려 있는 GE. AttackPower를 곱연산으로 올리는 Infinite GE를 지정한다.
	//배수는 GE 에셋에서 정한다. 데미지 계산은 UExecCalc_Damage가 AttackPower를 캡처해서 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camo")
	TSubclassOf<UGameplayEffect> EmpowerEffectClass;

	//장막이 도는 동안 걸려 있는 Has Duration GE. State.Skill.Camo.Veil을 부여한다.
	//지속 시간은 VeilDuration으로 덮어쓰므로 GE 에셋의 Duration 값은 무시된다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camo")
	TSubclassOf<UGameplayEffect> VeilEffectClass;

private:
	void EnterStealth();
	void ExitStealth();
	void OnVeilExpired();

	//UI가 남은 시간을 읽을 수 있게 장막을 GE로도 건다. 타이머는 서버에만 있어 클라이언트가 못 본다
	void ApplyVeilEffect();

	UFUNCTION()
	void OnWeaponFired(FGameplayEventData Payload);

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitGameplayEvent> FiredEventTask;

	FActiveGameplayEffectHandle EmpowerHandle;
	FActiveGameplayEffectHandle VeilHandle;
	FTimerHandle VeilTimerHandle;
	FTimerHandle StealthTimerHandle;
};
