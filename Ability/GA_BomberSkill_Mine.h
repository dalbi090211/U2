#pragma once

#include "CoreMinimal.h"
#include "GA_CharacterSkillBase.h"
#include "GA_BomberSkill_Mine.generated.h"

class ABomberMine;
class UAnimMontage;
class UGameplayEffect;

/*
 * 폭파병 지뢰의 입력과 연출 담당.
 *
 *   1번째 입력 → 투척 몽타주 시작 → 손을 떠나는 프레임의 AnimNotify에서
 *                지뢰 스폰, 지속시간 GE 부여, 기본공격 차단 해제, EndAbility
 *   2번째 입력 → 폭파 이벤트 발송(UGA_BomberSkill_MineDetonate가 받는다), 기폭 몽타주, 즉시 EndAbility
 *
 * 폭파 입력은 지뢰가 손을 떠난 뒤에야 들어온다. 그 전까지는 어빌리티가 살아 있다.
 * 남은 시간은 지속시간 GE가 들고 있고, 걷어내는 것은 ABomberMine::Detonate 한 곳이다.
 *
 * 몽타주가 시전자 본인에게도 보여야 해서 LocalPredicted로 돈다. ServerOnly면
 * 복제된 몽타주를 locally controlled 쪽에서 재생하지 않는다.
 * 그래서 두 입력의 분기는 인스턴스 멤버가 아니라 양쪽이 같이 보는 태그로 판단한다
 */
UCLASS()
class TEAMPROJECT_FIN_API UGA_BomberSkill_Mine : public UGA_CharacterSkillBase
{
	GENERATED_BODY()

public:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

protected:
	//투척 몽타주의 AnimNotify(Send Gameplay Event)가 보내는 이벤트. 이 프레임에 지뢰가 나간다
	UFUNCTION()
	void OnThrowNotify(FGameplayEventData Payload);

	//지뢰가 살아 있는 동안은 쿨다운을 건너뛴다. 두 번째 입력이 폭파로 가야 한다
	virtual bool CheckCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	//데미지/폭발 반경은 이 클래스가 아니라 지뢰 BP에서 정한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine")
	TSubclassOf<ABomberMine> MineClass;

	//State.Skill.Duration과 State.Skill.Bomber.MineArmed를 부여하는 Has Duration GE.
	//지속 시간은 ArmedSeconds로 덮어쓰므로 GE 에셋의 Duration 값은 무시된다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine")
	TSubclassOf<UGameplayEffect> ArmedEffectClass;

	//지뢰가 스스로 터질 때까지의 시간. 신관과 지속시간 GE가 같이 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine", meta = (ClampMin = "0.1"))
	float ArmedSeconds = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Throw", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ThrowSpeed = 1400.0f;

	//조준선보다 이만큼 위로 던진다. 발밑에 떨어지지 않게 하는 값
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Throw")
	float ThrowPitchOffset = 12.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Throw", meta = (ClampMin = "0.0"))
	float ThrowForwardOffset = 80.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Throw")
	float ThrowHeightOffset = 40.0f;

	//연출용. 어빌리티는 몽타주를 기다리지 않고 끝나며, 몽타주는 그대로 재생된다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Montage")
	TObjectPtr<UAnimMontage> ThrowMontage;

	//기폭 장치를 누르는 동작. 비워두면 아무것도 재생하지 않는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Montage")
	TObjectPtr<UAnimMontage> DetonateMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Montage", meta = (ClampMin = "0.01"))
	float MontagePlayRate = 1.0f;

private:
	void ThrowMine(AActor* AvatarActor);
	void ApplyArmedEffect(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo);
	void SendDetonateEvent(AActor* AvatarActor);

	//스폰과 GE 부여, 차단 해제, EndAbility를 한다. 노티파이와 예비 타이머가 같이 부른다
	void ReleaseMine();

	//재생 시간을 초로 돌려준다. 몽타주가 없으면 0
	float PlaySkillMontage(
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		UAnimMontage* Montage);

	//서버에만 값이 들어간다. 클라이언트는 지뢰를 스폰하지 않는다
	TWeakObjectPtr<ABomberMine> ActiveMine;

	//몽타주가 끝나면 기본공격 차단 태그를 걷는다
	FTimerHandle MontageTagTimerHandle;

	//노티파이가 없거나 몽타주가 끊겨도 지뢰는 나가야 한다. 몽타주 끝에서 대신 던진다
	FTimerHandle ThrowFallbackTimerHandle;
};
