// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerAnimInstance.h"

#include "../Character/BaseCharacter.h"
#include "../Character/PlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "../Tags/GameplayTagSystem.h"
#include "../Weapon/Core/WeaponBase.h"
#include "../Weapon/Core/WeaponComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

UPlayerAnimInstance::UPlayerAnimInstance()
{
}

void UPlayerAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwnerCharacter = Cast<ABaseCharacter>(TryGetPawnOwner());
	if (OwnerCharacter.IsValid())
	{
		MovementComponent = OwnerCharacter->GetCharacterMovement();
		BindAbilitySystemEvents();
	}
}

void UPlayerAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwnerCharacter.IsValid())
	{
		OwnerCharacter = Cast<ABaseCharacter>(TryGetPawnOwner());
		if (OwnerCharacter.IsValid())
		{
			MovementComponent = OwnerCharacter->GetCharacterMovement();
		}
	}

	if (!OwnerCharacter.IsValid() || !MovementComponent.IsValid())
	{
		return;
	}

	BindAbilitySystemEvents();
	UpdateLocomotionValues(DeltaSeconds);
	UpdateAimOffsetValues(DeltaSeconds);
	UpdateLeftHandIK();
	UpdateLeftHandIKAlpha(DeltaSeconds);
}

void UPlayerAnimInstance::StartLeftHandIKRecovery()
{
	bLeftHandIKRecovering = true;
}

void UPlayerAnimInstance::BindAbilitySystemEvents()
{
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	UAbilitySystemComponent* ASC = OwnerCharacter->GetAbilitySystemComponent();
	if (!ASC || BoundAbilitySystemComponent.Get() == ASC)
	{
		return;
	}

	if (UAbilitySystemComponent* PreviousASC = BoundAbilitySystemComponent.Get())
	{
		UnbindGameplayTagEvent(PreviousASC, GameplayTagSystem::State_Movement_Sprinting);
		UnbindGameplayTagEvent(PreviousASC, GameplayTagSystem::State_Weapon_Aiming);
		UnbindGameplayTagEvent(PreviousASC, GameplayTagSystem::State_Dead);
	}

	BoundAbilitySystemComponent = ASC;

	BindGameplayTagEvent(ASC, GameplayTagSystem::State_Movement_Sprinting, &UPlayerAnimInstance::OnSprintingTagChanged);
	BindGameplayTagEvent(ASC, GameplayTagSystem::State_Weapon_Aiming, &UPlayerAnimInstance::OnAimingTagChanged);
	BindGameplayTagEvent(ASC, GameplayTagSystem::State_Dead, &UPlayerAnimInstance::OnDeadTagChanged);
}

void UPlayerAnimInstance::BindGameplayTagEvent(UAbilitySystemComponent* ASC, const FGameplayTag& Tag, void (UPlayerAnimInstance::*Callback)(const FGameplayTag, int32))
{
	if (!ASC || !Tag.IsValid())
	{
		return;
	}

	ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
	ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, Callback);
}

void UPlayerAnimInstance::UnbindGameplayTagEvent(UAbilitySystemComponent* ASC, const FGameplayTag& Tag)
{
	if (!ASC || !Tag.IsValid())
	{
		return;
	}

	ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
}

void UPlayerAnimInstance::UpdateLocomotionValues(float DeltaSeconds)
{
	Velocity = MovementComponent->Velocity;
	GroundSpeed = Velocity.Size2D();
	Acceleration = MovementComponent->GetCurrentAcceleration();

	bHasAcceleration = Acceleration.SizeSquared2D() > 0.0f;
	bIsMoving = GroundSpeed > 3.0f;
	
	bShouldMove = bHasAcceleration && bIsMoving;

	// 이동 중일 때만 걷기/뛰기 상태를 업데이트하여 Stop 진입 시 마지막 속도 상태 유지
	if (bShouldMove)
	{
		bWasSprinting = (GroundSpeed > 250.0f);
	}

	// 캐릭터 정면 회전 대비 이동 방향 각도 계산 (-180 ~ +180도)
	// [다리 부들거림 픽스] 속도가 거의 0일 때 Velocity 벡터는 미세한 마찰/물리로 인해 사방으로 튀기 때문에 Direction이 홱홱 바뀝니다.
	// 따라서 의미 있는 속도(GroundSpeed > 3.0f)일 때만 Direction을 업데이트하여 출발(Start) 시 다리 블렌드스페이스가 요동치는 것을 막습니다.
	if (bIsMoving)
	{
		Direction = CalculateDirection(Velocity, OwnerCharacter->GetActorRotation());
	}

	// [핵심 로직] 속도(관성)와 가속도(입력)의 방향이 얼추 일치할 때(정상 주행 중)만 방향을 저장합니다 (Stop 노드용).
	if (bHasAcceleration && GroundSpeed > 50.0f)
	{
		const FVector NormVel = Velocity.GetSafeNormal2D();
		const FVector NormAcc = Acceleration.GetSafeNormal2D();

		// 내적이 0.5 이상(약 60도 이내)일 때만 업데이트
		if (FVector::DotProduct(NormVel, NormAcc) > 0.5f)
		{
			SavedMovementDirection = Direction;
		}
	}

	// 자세 및 상태 업데이트
	bIsCrouching = MovementComponent->IsCrouching();
	bIsFalling = MovementComponent->IsFalling();
	bIsGrounded = !bIsFalling;

	// 피벗(급격한 방향 전환) 감지: 일정 속도 이상 이동 중 가속도와 속도의 내적이 음수일 때
	if (bHasAcceleration && GroundSpeed > 150.0f)
	{
		const FVector NormalizedVelocity = Velocity.GetSafeNormal2D();
		const FVector NormalizedAcceleration = Acceleration.GetSafeNormal2D();
		const float DotProduct = FVector::DotProduct(NormalizedVelocity, NormalizedAcceleration);

		if (DotProduct < -0.2f)
		{
			// [수정됨] 언리얼 내장 함수 대신, 수학적으로 100% 정확한 로컬 요(Yaw) 각도를 직접 계산합니다.
			// 1. 월드 공간의 가속도(입력)를 캐릭터가 바라보는 기준(로컬 공간)으로 역변환합니다.
			const FVector LocalAccel = OwnerCharacter->GetActorRotation().UnrotateVector(Acceleration);
			
			// 2. Atan2를 사용하여 정확한 방향 각도(-180 ~ 180)를 추출합니다.
			const float PivotAngleRaw = FMath::RadiansToDegrees(FMath::Atan2(LocalAccel.Y, LocalAccel.X));

			// [수정됨] if (!bIsPivot) 잠금을 해제하여, 피벗 중 입력이 바뀌어도 즉각적으로 올바른 상태를 찾도록 합니다.
			if (PivotAngleRaw >= -45.0f && PivotAngleRaw <= 45.0f)
			{
				CurrentPivotDirection = EPivotDirection::Forward;
			}
			else if (PivotAngleRaw > 45.0f && PivotAngleRaw <= 135.0f)
			{
				CurrentPivotDirection = EPivotDirection::Right;
			}
			else if (PivotAngleRaw >= -135.0f && PivotAngleRaw < -45.0f)
			{
				CurrentPivotDirection = EPivotDirection::Left;
			}
			else
			{
				CurrentPivotDirection = EPivotDirection::Backward;
			}

			bIsPivot = true;
			PivotAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));
		}
		else
		{
			bIsPivot = false;
			PivotAngle = 0.0f;
		}
	}
	else
	{
		bIsPivot = false;
		PivotAngle = 0.0f;
	}
}

void UPlayerAnimInstance::OnSprintingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	// 태그 델리게이트 이벤트가 발생했을 때만 단 1번 호출됨
	bIsSprinting = (NewCount > 0);
}

void UPlayerAnimInstance::OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	bIsAiming = (NewCount > 0);
}

void UPlayerAnimInstance::OnDeadTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	bIsDead = (NewCount > 0);
	if (!bIsDead)
	{
		bUseDeathPose = false;
	}
}

void UPlayerAnimInstance::UpdateAimOffsetValues(float DeltaSeconds)
{
	float TargetPitch = 0.0f;
	float TargetYaw = 0.0f;

	// [크로스헤어 보정] 로컬 플레이어는 카메라 중심선(크로스헤어)을 쳐다보도록 정밀 계산된 각도를 사용합니다.
	if (OwnerCharacter->IsLocallyControlled())
	{
		if (APlayerCharacter* PlayerChar = Cast<APlayerCharacter>(OwnerCharacter))
		{
			PlayerChar->GetAimOffsetAngles(TargetPitch, TargetYaw);
			TargetPitch = FMath::Clamp(TargetPitch, -90.0f, 90.0f);
			
			if (TargetYaw > 90.0f) TargetYaw = 180.0f - TargetYaw;
			else if (TargetYaw < -90.0f) TargetYaw = -180.0f - TargetYaw;
		}
	}
	else
	{
		// [네트워크 동기화] 원격 클라이언트(Simulated Proxy)는 카메라가 없으므로 서버가 복제해준 회전값을 사용합니다.
		const FRotator AimRotation = OwnerCharacter->GetBaseAimRotation();
		const FRotator ActorRotation = OwnerCharacter->GetActorRotation();

		const float NormalizedPitch = FRotator::NormalizeAxis(AimRotation.Pitch);
		TargetPitch = FMath::Clamp(NormalizedPitch, -90.0f, 90.0f);

		float RealAimYaw = AimRotation.Yaw;
		if (OwnerCharacter->GetLocalRole() == ROLE_SimulatedProxy)
		{
			if (APlayerCharacter* PlayerChar = Cast<APlayerCharacter>(OwnerCharacter))
			{
				RealAimYaw = PlayerChar->ReplicatedControlYaw;
			}
		}

		const FRotator RealAimRot(AimRotation.Pitch, RealAimYaw, 0.0f);
		const FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(RealAimRot, ActorRotation);
		TargetYaw = DeltaRot.Yaw;

		if (TargetYaw > 90.0f) TargetYaw = 180.0f - TargetYaw;
		else if (TargetYaw < -90.0f) TargetYaw = -180.0f - TargetYaw;
	}

	// 로컬 좌우 상대각은 즉시 적용해 몸이 돌아간 뒤 이전 오프셋이 남지 않도록 한다.
	// 상하 보정, 달리기 복귀, 원격 플레이어의 기존 보간은 유지한다.
	const bool bUseRelativeAimYaw = OwnerCharacter->IsLocallyControlled() && !bIsSprinting;
	AimPitch = FMath::FInterpTo(AimPitch, TargetPitch, DeltaSeconds, 15.0f);
	AimYaw = bUseRelativeAimYaw ? TargetYaw : FMath::FInterpTo(AimYaw, TargetYaw, DeltaSeconds, 15.0f);
}

void UPlayerAnimInstance::UpdateLeftHandIK()
{
	bHasLeftHandIK = false;
	LeftHandIKTransform = FTransform::Identity;

	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	UWeaponComponent* WeaponComponent =
		OwnerCharacter->GetWeaponComponent();

	if (!WeaponComponent)
	{
		return;
	}

	AWeaponBase* Weapon =
		WeaponComponent->GetEquippedWeapon();

	if (!Weapon)
	{
		return;
	}

	UStaticMeshComponent* WeaponMesh =
		Weapon->GetBarrelMesh();

	if (!WeaponMesh)
	{
		return;
	}

	static const FName HandSocketName(TEXT("HandSocket"));
	static const FName GunIKBoneName(TEXT("ik_hand_gun"));

	if (!WeaponMesh->DoesSocketExist(HandSocketName))
	{
		return;
	}

	USkeletalMeshComponent* CharacterMesh =
		GetSkelMeshComponent();

	if (!CharacterMesh)
	{
		return;
	}

	if (!CharacterMesh->DoesSocketExist(GunIKBoneName))
	{
		return;
	}

	const FTransform HandSocketWorldTransform =
		WeaponMesh->GetSocketTransform(
			HandSocketName,
			RTS_World
		);

	const FTransform GunIKWorldTransform =
		CharacterMesh->GetSocketTransform(
			GunIKBoneName,
			RTS_World
		);

	LeftHandIKTransform =
		HandSocketWorldTransform.GetRelativeTransform(
			GunIKWorldTransform
		);

	bHasLeftHandIK = true;
}

void UPlayerAnimInstance::UpdateLeftHandIKAlpha(float DeltaSeconds)
{
	if (!OwnerCharacter.IsValid() || !bHasLeftHandIK)
	{
		TargetLeftHandIKAlpha = 0.0f;

		LeftHandIKAlpha =
			FMath::FInterpTo(
				LeftHandIKAlpha,
				TargetLeftHandIKAlpha,
				DeltaSeconds,
				LeftHandIKInterpSpeed
			);

		return;
	}

	bool bIsReloading = false;
	bool bIsReviving = false;

	if (UAbilitySystemComponent* ASC = OwnerCharacter->GetAbilitySystemComponent())
	{
		bIsReloading = ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Weapon_Reloading);
		bIsReviving = ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Reviving);
	}

	if (bIsDead || bIsReviving)
	{
		// 사망 또는 부활 시 손이 꺾이는 것을 방지하기 위해 IK 즉시 해제
		TargetLeftHandIKAlpha = 0.0f;
		LeftHandIKAlpha = 0.0f; // 즉각적으로 풀리게 Interp 생략 가능
		return;
	}

	// 장전이 새로 시작되는 순간
	if (bIsReloading && !bWasReloading)
	{
		// 아직 왼손 IK 복귀 허용 안 함
		bLeftHandIKRecovering = false;
	}

	if (bIsReloading)
	{
		// 장전 중
		// Recover Notify 전  -> IK OFF
		// Recover Notify 이후 -> IK ON
		TargetLeftHandIKAlpha =
			bLeftHandIKRecovering
			? 1.0f
			: 0.0f;
	}
	else
	{
		// 평상시는 항상 IK 사용
		TargetLeftHandIKAlpha = 1.0f;
	}

	static const FName DisableIKCurveName(TEXT("DisableLHandIK"));
	const float DisableCurve = GetCurveValue(DisableIKCurveName);
	TargetLeftHandIKAlpha *= (1.0f - FMath::Clamp(DisableCurve, 0.0f, 1.0f));

	LeftHandIKAlpha =
		FMath::FInterpTo(
			LeftHandIKAlpha,
			TargetLeftHandIKAlpha,
			DeltaSeconds,
			LeftHandIKInterpSpeed
		);

	bWasReloading = bIsReloading;
}
