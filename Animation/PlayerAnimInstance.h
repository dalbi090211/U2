// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameCore/Animation/BaseCharacterAnimInstance.h"
#include "PlayerAnimInstance.generated.h"

UENUM(BlueprintType)
enum class EPivotDirection : uint8
{
	Forward UMETA(DisplayName = "Forward"),
	Backward UMETA(DisplayName = "Backward"),
	Left UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right")
};

class ABaseCharacter;
class UCharacterMovementComponent;
class UAbilitySystemComponent;

/**
 * 플레이어 캐릭터의 애니메이션 블루프린트용 C++ 베이스 클래스
 * 모든 계산은 C++에서 수행하며, 애님 블루프린트(ABP)에서는 변수를 읽기만(Read-Only) 합니다.
 */
UCLASS()
class TEAMPROJECT_FIN_API UPlayerAnimInstance : public UBaseCharacterAnimInstance
{
	GENERATED_BODY()

public:
	UPlayerAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	void StartLeftHandIKRecovery();

protected:
	/** 내부 연산 헬퍼 함수 */
	void UpdateLocomotionValues(float DeltaSeconds);
	void UpdateAimOffsetValues(float DeltaSeconds);

	/** GAS 태그 변화 이벤트 콜백 */
	void OnSprintingTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount);
	void OnDeadTagChanged(const FGameplayTag Tag, int32 NewCount);

	void UpdateLeftHandIK();
	void UpdateLeftHandIKAlpha(float DeltaSeconds);
	void BindAbilitySystemEvents();
	void BindGameplayTagEvent(UAbilitySystemComponent* ASC, const struct FGameplayTag& Tag, void (UPlayerAnimInstance::*Callback)(const struct FGameplayTag, int32));
	void UnbindGameplayTagEvent(UAbilitySystemComponent* ASC, const struct FGameplayTag& Tag);

protected:
	/* -------------------------------------------------------------------------
	 * 캐싱된 참조 포인터
	 * ------------------------------------------------------------------------- */
	UPROPERTY(BlueprintReadOnly, Category = "References")
	TWeakObjectPtr<ABaseCharacter> OwnerCharacter;

	UPROPERTY(BlueprintReadOnly, Category = "References")
	TWeakObjectPtr<UCharacterMovementComponent> MovementComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystemComponent;

	/* -------------------------------------------------------------------------
	 * 기본 이동 및 속도 변수 (Locomotion)
	 * ------------------------------------------------------------------------- */
	 /** 수평 이동 속력 (Size2D) - BlendSpace 축 값으로 사용 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	float GroundSpeed = 0.0f;

	/** 이동 속도 벡터 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FVector Velocity = FVector::ZeroVector;

	/** 입력 가속도 벡터 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	FVector Acceleration = FVector::ZeroVector;

	/** 캐릭터 기준 이동 방향 각도 (-180 ~ +180도) - 스트레이프 BlendSpace 축 값으로 사용 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	float Direction = 0.0f;

	/** 지상 속도가 일정 임계값(3.0) 이상인지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	bool bIsMoving = false;

	/** 가속도 입력이 존재하는지 여부 (Size2D > 0) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	bool bHasAcceleration = false;

	/** 가속도 입력이 있고 실제로 이동 중인지 여부 (Start / Stop 상태 전이의 핵심 조건) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	bool bShouldMove = false;

	/** 정지 상태에 진입할 때 뛰고 있었는지 걷고 있었는지 판별용 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	bool bWasSprinting = false;



	/** 현재 조준(Aiming) 중인지 여부 - GAS 태그로만 업데이트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	bool bIsAiming = false;

	/** 현재 사망(Dead) 상태인지 여부 - GAS 태그 델리게이트로 업데이트 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Posture")
	bool bIsDead = false;

	/* -------------------------------------------------------------------------
	 * Aim Offset (상체 회전) 관련 변수
	 * ------------------------------------------------------------------------- */
	 /** 이동 중 급격하게 반대/측면 방향으로 전환하는 순간 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	bool bIsPivot = false;

	/** 피벗/정지 시 사용할 확정된 방향 상태 (블렌드 스페이스 튐 방지용 Enum) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	EPivotDirection CurrentPivotDirection = EPivotDirection::Forward;

	/** 정지 시 애니메이션 튐 방지를 위해, 마지막으로 안정적이던 이동(관성) 방향을 기록 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	float SavedMovementDirection = 0.0f;


	/** 피벗 시 속도 방향과 가속도 방향 사이의 각도 차이 (0 ~ 180도) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Locomotion")
	float PivotAngle = 0.0f;

	/* -------------------------------------------------------------------------
	 * 상태 및 자세 변수 (States & Posture)
	 * ------------------------------------------------------------------------- */
	 /** 스프린팅(달리기) 상태 여부 (GAS 태그 델리게이트로 업데이트되어 틱 낭비 없음) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Posture")
	bool bIsSprinting = false;

	/** 앉기(크라우치) 상태 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Posture")
	bool bIsCrouching = false;

	/** 공중 낙하 상태 여부 (점프 없음, 지형 추락/낙하 감지) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Posture")
	bool bIsFalling = false;

	/** 지면에 닿아있는 상태인지 여부 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Posture")
	bool bIsGrounded = true;

	/* -------------------------------------------------------------------------
	 * 에임 오프셋 변수 (Aim Offset)
	 * ------------------------------------------------------------------------- */
	 /** 현재 적용 중인 최종 조준 상하 각도 */
	// Interp — 컷신 대역(소유 캐릭터가 없어 이 값을 갱신하지 않는다)이 시퀀서에서 키를 찍어 시선을 움직일 수 있다
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "AimOffset")
	float AimPitch = 0.0f;

	/** 현재 적용 중인 최종 조준 좌우 각도 */
	// Interp 인 이유는 AimPitch 와 같다
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "AimOffset")
	float AimYaw = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|IK")
	FTransform LeftHandIKTransform = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|IK")
	bool bHasLeftHandIK = false;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon|IK")
	float LeftHandIKAlpha = 1.0f;

	float TargetLeftHandIKAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|IK")
	float LeftHandIKInterpSpeed = 10.0f;

	/** 장전 후반부에서 왼손 IK 복귀가 시작됐는지 */
	bool bLeftHandIKRecovering = false;

	/** 장전 시작 순간 감지용 */
	bool bWasReloading = false;

};
