// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCharacter.h"
#include "GameCore/Audio/MusicDirectorSubsystem.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundEffectSubmix.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "../Attribute/CharacterAttributeSet.h"
#include "../BasePlayerState.h"
#include "TimerManager.h"
#include "../Tags/GameplayTagSystem.h"
#include "../../snowdeformation_V2/SnowDeformationComponentV2.h"
#include "Net/UnrealNetwork.h"
#include "GameCore/Interact/InteractComponent.h"
#include "GameCore/Weapon/Libraries/WeaponRecoilLibrary.h"
#include "Environment/EnvironmentCharacterMovementComponent.h"
#include "GameCore/Weapon/Trajectory/WeaponTrajectoryPreviewComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameCore/Weapon/Core/WeaponComponent.h"
#include "GameCore/Weapon/Core/WeaponBase.h"
#include "PCGMap/MapBuild/SectorBuild/Event/PlacedEvent/BattleSector/OverlapBattleSector.h"
#include "Environment/RopeAnchorActor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameCore/Settings/ProjectInputSettingsSubsystem.h"
#include "EngineGlobals.h"

APlayerCharacter::APlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UEnvironmentCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	CharacterRoleTag = GameplayTagSystem::Character_Player;

	SnowDeformationComponentV2 = CreateDefaultSubobject<USnowDeformationComponentV2>(TEXT("SnowDeformationComponentV2"));

	// 틱(Tick) 함수 활성화 (회전 모드 등 매 프레임 업데이트용)
	PrimaryActorTick.bCanEverTick = true;

	// 컨트롤러 Yaw 회전에 맞춰 캐릭터가 항상 카메라 방향을 바라보도록 설정 (TPS 슈터 / 스트레이프 방식)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false; // 이벤트에서 제어되므로 초기값 false
	bUseControllerRotationRoll = false;

	// 이동 방향으로의 독립 회전 (조금 더 빠르고 민첩하게 돌도록 수치 상향)
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	GetCharacterMovement()->JumpZVelocity = 500.0f;
	GetCharacterMovement()->AirControl = 0.35f;

	// [중요] 앉기(Crouch) 기능을 사용하기 위한 필수 설정 (이 설정이 켜져있어야 엔진 기본 캡슐 반갈죽이 작동함)
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;

	GetCharacterMovement()->MaxWalkSpeed = 300.0f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// 스켈레탈 메시 피격 판정 세팅 (플레이어용)
	if (USkeletalMeshComponent *MeshComp = GetMesh())
	{
		// 플레이어/몬스터 공격 판정은 모두 TraceAttack 채널로 조회한다.
		MeshComp->SetCollisionResponseToChannel(ECC_TraceAttack, ECR_Block);

		// 카메라 은폐(OwnerNoSee) 발동 시에도 그림자는 계속 렌더링되도록 강제
		MeshComp->bCastHiddenShadow = true;
	}

	// 3인칭 스프링암(CameraBoom) 생성 및 세팅
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 140.0f;
	CameraBoom->bUsePawnControlRotation = true;				// 컨트롤러 시점 회전 적용
	CameraBoom->bDoCollisionTest = true;					// 카메라 지형 충돌 감지 활성화
	CameraBoom->SocketOffset = FVector(0.0f, 60.0f, 20.0f); // TPS 숄더뷰 기본 오프셋

	// 팔로우 카메라 생성 및 스프링암에 부착
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false; // 스프링암을 따르므로 카메라 자체는 회전하지 않음

	// 상호작용 Invoker
	InteractComponent = CreateDefaultSubobject<UInteractComponent>(TEXT("InteractComponent"));

	TrajectoryPreviewComponent = CreateDefaultSubobject<UWeaponTrajectoryPreviewComponent>(TEXT("TrajectoryPreviewComponent"));

	LastCheckpointLocation = FVector::ZeroVector;
	static ConstructorHelpers::FObjectFinder<USoundSubmix> Submix(TEXT("/Game/TPSDevelop/Class/Skill/SM_Focus"));
	static ConstructorHelpers::FObjectFinder<USoundEffectSubmixPreset> Filter(TEXT("/Game/TPSDevelop/Class/Skill/SEP_MuffleLP"));
	FocusSubmix = Submix.Object;
	FocusFilter = Filter.Object;
}

void APlayerCharacter::ServerRequestBattleSectorActivation_Implementation(AOverlapBattleSector* BattleSector)
{
	if (IsValid(BattleSector))
	{
		BattleSector->RequestServerActivation(this);
	}
}

void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (CameraBoom && GetMesh())
	{
		// 이동과 몸 회전 후 카메라를 먼저 갱신해 에임 계산이 중간 카메라 변환을 읽지 않도록 한다.
		CameraBoom->PrimaryComponentTick.TickGroup = TG_PrePhysics;
		CameraBoom->AddTickPrerequisiteActor(this);
		CameraBoom->AddTickPrerequisiteComponent(GetCharacterMovement());
		GetMesh()->AddTickPrerequisiteComponent(CameraBoom);
	}

	// 시작 시 카메라 FOV 초기화 및 목표 FOV 동기화
	if (FollowCamera)
	{
		FollowCamera->SetFieldOfView(DefaultFOV);
		TargetFOV = DefaultFOV;
	}

	// 팀원 체력바용 속성 변화 감지 바인딩
	if (AbilitySystemComponent)
	{
		// 팀원 체력바용. 최대 체력이 바뀌면 비율이 달라지므로 둘 다 감시한다.
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetHealthAttribute())
			.AddUObject(this, &APlayerCharacter::OnVitalsAttributeChanged);

		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetMaxHealthAttribute())
			.AddUObject(this, &APlayerCharacter::OnVitalsAttributeChanged);

		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetHealthCapMultiplierAttribute())
			.AddUObject(this, &APlayerCharacter::OnVitalsAttributeChanged);
	}
	RefreshAbilitySystemBinding();

	PushVitalsToPlayerState();

	RegisterInputMappingContext();

	// 최초 스폰 위치를 기본 체크포인트로 지정
	LastCheckpointLocation = GetActorLocation();
}

void APlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopCamoSound();
	SetFocusAudio(false);
	ResetPersistentInput();
	UnbindInputSettings();
	Super::EndPlay(EndPlayReason);
}

void APlayerCharacter::UnPossessed()
{
	StopCamoSound();
	SetFocusAudio(false);
	ResetPersistentInput();
	UnbindInputSettings();
	Super::UnPossessed();
}

void APlayerCharacter::SetCheckpoint(FVector NewLocation)
{
	LastCheckpointLocation = NewLocation;
}

void APlayerCharacter::RespawnAtCheckpoint()
{
	if (!LastCheckpointLocation.IsNearlyZero())
	{
		// 순간이동 전, 추락 가속도 등 모든 속도를 초기화하여 텔레포트 후 다시 떨어지지 않도록 함
		if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		{
			CMC->StopMovementImmediately();
		}
		
		SetActorLocation(LastCheckpointLocation);
	}
}

void APlayerCharacter::PossessedBy(AController *NewController)
{
	Super::PossessedBy(NewController);

	RefreshAbilitySystemBinding();
	PushVitalsToPlayerState();
	RefreshAbilitySystemBinding();

	UpdateCameraFadeSettings();
}

void APlayerCharacter::OnVitalsAttributeChanged(const FOnAttributeChangeData &Data)
{
	PushVitalsToPlayerState();
}

void APlayerCharacter::PushVitalsToPlayerState()
{
	// 값의 출처는 서버 한 곳뿐이다. 클라이언트는 복제된 사본만 읽는다.
	if (!HasAuthority())
	{
		return;
	}

	UCharacterAttributeSet *Attributes = GetCharacterAttributeSet();
	ABasePlayerState *PS = GetPlayerState<ABasePlayerState>();

	if (!Attributes || !PS)
	{
		UE_LOG(LogTemp, Error, TEXT("[PushVitalsToPlayerState] Attributes or PS is NULL!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[PushVitalsToPlayerState] Called! Health: %f"), Attributes->GetHealth());
	PS->SetVitals(Attributes->GetHealth(), Attributes->GetMaxHealth(), Attributes->GetHealthCapMultiplier());
}

void APlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlayerCharacter, ReplicatedControlYaw);
	DOREPLIFETIME(APlayerCharacter, CurrentSwingingAnchor);
}

void APlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	RefreshAbilitySystemBinding();
	RegisterInputMappingContext();

	UpdateCameraFadeSettings();
}

void APlayerCharacter::UpdateCameraFadeSettings()
{
	if (IsLocallyControlled())
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			MeshComp->SetCustomPrimitiveDataFloat(0, 1.0f);
		}

		if (UWeaponComponent* WeaponComp = FindComponentByClass<UWeaponComponent>())
		{
			if (AWeaponBase* Weapon = WeaponComp->GetEquippedWeapon())
			{
				TArray<UMeshComponent*> WeaponMeshes;
				Weapon->GetComponents<UMeshComponent>(WeaponMeshes);
				for (UMeshComponent* WMesh : WeaponMeshes)
				{
					WMesh->SetCustomPrimitiveDataFloat(0, 1.0f);
					WMesh->bCastHiddenShadow = true;
				}
			}
		}
	}
}

void APlayerCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (CameraBoom)
	{
		// 캡슐이 줄어들면서 액터 기준점이 바닥으로 확 내려갔으므로,
		// 카메라 스프링암의 오프셋을 순간적으로 그만큼 위로 올려주어 시각적인 화면 튀어오름(어지러움)을 완전히 상쇄합니다.
		CameraBoom->TargetOffset.Z += HalfHeightAdjust;

		// 그 후, 유저가 설정한 카메라 하강 수치(CrouchCameraDropAmount)만큼만 목표 높이를 재설정합니다.
		TargetCameraZOffset = -CrouchCameraDropAmount;

		// 틱 대신 타이머를 활용하여 부드럽게 보간하여 내려갑니다.
		GetWorld()->GetTimerManager().SetTimer(CrouchCameraTimerHandle, this, &APlayerCharacter::UpdateCrouchCameraOffset, 0.016f, true);
	}

	// 앉기 상태가 물리적으로 완전히 적용된 시점에 회전 모드 동기화
	UpdateRotationMode();
}

void APlayerCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	if (CameraBoom)
	{
		// 캡슐이 늘어나면서 액터 기준점이 위로 확 올라갔으므로,
		// 스프링암 오프셋을 순간적으로 아래로 내려서 시각적인 화면 튀어오름을 상쇄합니다.
		CameraBoom->TargetOffset.Z -= HalfHeightAdjust;

		// 평상시 높이(0.0f)로 목표를 재설정합니다.
		TargetCameraZOffset = 0.0f;

		GetWorld()->GetTimerManager().SetTimer(CrouchCameraTimerHandle, this, &APlayerCharacter::UpdateCrouchCameraOffset, 0.016f, true);
	}

	// 일어서기 상태가 물리적으로 완전히 적용된 시점에 회전 모드 동기화
	UpdateRotationMode();
}

void APlayerCharacter::UpdateCrouchCameraOffset()
{
	if (!CameraBoom)
		return;

	const float CurrentZ = CameraBoom->TargetOffset.Z;
	// 설정한 보간 속도(CrouchCameraInterpSpeed)로 부드럽게 타겟을 향해 이동
	const float NewZ = FMath::FInterpTo(CurrentZ, TargetCameraZOffset, 0.016f, CrouchCameraInterpSpeed);

	CameraBoom->TargetOffset.Z = NewZ;

	// 목표치에 거의 도달하면 불필요한 연산(타이머)을 종료하여 성능 최적화
	if (FMath::IsNearlyEqual(CurrentZ, TargetCameraZOffset, 0.1f))
	{
		CameraBoom->TargetOffset.Z = TargetCameraZOffset;
		GetWorld()->GetTimerManager().ClearTimer(CrouchCameraTimerHandle);
	}
}

void APlayerCharacter::RegisterInputMappingContext()
{
	if (const APlayerController *PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem *Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultMappingContext && !Subsystem->HasMappingContext(DefaultMappingContext))
			{
				Subsystem->AddMappingContext(DefaultMappingContext, DefaultMappingPriority);
			}
		}
	}
}

void APlayerCharacter::RefreshAbilitySystemBinding()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
		BindPlayerGameplayTagEvents();
	}
	BindInputSettings();
}

void APlayerCharacter::BindPlayerGameplayTagEvents()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	BindGameplayTagEvent(GameplayTagSystem::State_Weapon_Aiming, &APlayerCharacter::OnAimingTagChanged);
	BindGameplayTagEvent(GameplayTagSystem::State_Movement_Dodging, &APlayerCharacter::OnInputBlockingTagChanged);
	BindGameplayTagEvent(GameplayTagSystem::State_Weapon_Reloading, &APlayerCharacter::OnInputBlockingTagChanged);
	BindGameplayTagEvent(GameplayTagSystem::State_Movement_Sprinting, &APlayerCharacter::OnInputBlockingTagChanged);
	BindGameplayTagEvent(GameplayTagSystem::State_Weapon_SmartTracking, &APlayerCharacter::OnInputBlockingTagChanged);
	BindGameplayTagEvent(GameplayTagSystem::State_Weapon_SmartTrackingVolley, &APlayerCharacter::OnInputBlockingTagChanged);
	BindGameplayTagEvent(GameplayTagSystem::State_Skill_Bomber_MineThrowing, &APlayerCharacter::OnInputBlockingTagChanged);
	BindGameplayTagEvent(GameplayTagSystem::State_Skill_Camo_Veil, &APlayerCharacter::OnCamoVeilTagChanged);
	OnCamoVeilTagChanged(GameplayTagSystem::State_Skill_Camo_Veil,
		AbilitySystemComponent->GetTagCount(GameplayTagSystem::State_Skill_Camo_Veil));
}

void APlayerCharacter::ClientPlayCamoSound_Implementation(USoundBase* Sound)
{
	StopCamoSound();
	if (IsLocallyControlled() && IsValid(Sound))
	{
		CamoSoundComponent = UGameplayStatics::SpawnSound2D(this, Sound);
	}
}

void APlayerCharacter::StopCamoSound()
{
	if (IsValid(CamoSoundComponent))
	{
		CamoSoundComponent->Stop();
	}
	CamoSoundComponent = nullptr;
}

void APlayerCharacter::OnCamoVeilTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (IsLocallyControlled())
	{
		SetFocusAudio(NewCount > 0);
	}
}

void APlayerCharacter::SetFocusAudio(bool bEnabled)
{
	if (bFocusAudioActive == bEnabled)
	{
		return;
	}
	checkf(FocusSubmix && FocusFilter, TEXT("[FocusAudio] SM_Focus / SEP_MuffleLP must be assigned"));
	UMusicDirectorSubsystem* Music = GetGameInstance()->GetSubsystem<UMusicDirectorSubsystem>();
	checkf(Music, TEXT("[FocusAudio] MusicDirectorSubsystem missing"));
	Music->SetMusicFocus(bEnabled);
	if (bEnabled)
	{
		UAudioMixerBlueprintLibrary::SetSubmixEffectChainOverride(this, FocusSubmix, { FocusFilter.Get() }, 0.2f);
	}
	else
	{
		UAudioMixerBlueprintLibrary::ClearSubmixEffectChainOverride(this, FocusSubmix, 0.7f);
	}
	bFocusAudioActive = bEnabled;
}

void APlayerCharacter::BindGameplayTagEvent(const FGameplayTag &Tag, void (APlayerCharacter::*Callback)(const FGameplayTag, int32))
{
	if (!AbilitySystemComponent || !Tag.IsValid())
	{
		return;
	}

	AbilitySystemComponent->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
	AbilitySystemComponent->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, Callback);
}

void APlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	RefreshAbilitySystemBinding();
}

void APlayerCharacter::OnRep_SwingingAnchor()
{
	// 1. 서버에서 밧줄을 잡았다고 동기화되어 날아온 경우
	if (CurrentSwingingAnchor)
	{
		// 클라이언트(Simulated Proxy) 화면에서 밧줄 핀을 이 캐릭터에 강제 고정
		CurrentSwingingAnchor->GrabRope(this, 1.0f); // Ratio 1.0f (가장 아래쪽) 가정
		
		// 나중에 밧줄을 놓을 때 추적하기 위해 로컬 변수에 앵커를 백업해 둠
		PreviousSwingingAnchor = CurrentSwingingAnchor;

		// 다른 플레이어(클라이언트) 화면에서도 무기를 숨김
		SetWeaponHidden(true);
	}
	// 2. 서버에서 밧줄을 놓았다고 동기화(nullptr)되어 날아온 경우
	else
	{
		// 백업해둔 앵커가 있다면 물리 핀 고정을 해제함
		if (PreviousSwingingAnchor)
		{
			PreviousSwingingAnchor->ReleaseRope(this);
			PreviousSwingingAnchor = nullptr; // 백업 초기화
		}

		// 무기 다시 표시
		SetWeaponHidden(false);
	}
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BindInputSettings();

	if (UEnhancedInputComponent *EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// 이동 입력 바인딩 (IA_Move)
		if (MoveAction)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &APlayerCharacter::StopMove);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &APlayerCharacter::StopMove);
		}

		// 시점 회전 입력 바인딩 (IA_Look)
		if (LookAction)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
		}

		// 구르기 입력 바인딩 (IA_Dodge) - 기존 점프 대체
		if (DodgeAction)
		{
			EnhancedInputComponent->BindAction(DodgeAction, ETriggerEvent::Started, this, &APlayerCharacter::StartDodge);
		}

		// 달리기 입력 바인딩 (IA_Sprint)
		if (SprintAction)
		{
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &APlayerCharacter::HandlePersistentInputStarted, EProjectInputAction::Sprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &APlayerCharacter::HandlePersistentInputCompleted, EProjectInputAction::Sprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Canceled, this, &APlayerCharacter::HandlePersistentInputCanceled, EProjectInputAction::Sprint);
		}

		// 앉기 입력 바인딩 (IA_Crouch)
		if (CrouchAction)
		{
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &APlayerCharacter::HandlePersistentInputStarted, EProjectInputAction::Crouch);
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &APlayerCharacter::HandlePersistentInputCompleted, EProjectInputAction::Crouch);
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Canceled, this, &APlayerCharacter::HandlePersistentInputCanceled, EProjectInputAction::Crouch);
		}

		// 조준 입력 바인딩 (IA_Aim)
		if (AimAction)
		{
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Started, this, &APlayerCharacter::HandlePersistentInputStarted, EProjectInputAction::Aim);
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Completed, this, &APlayerCharacter::HandlePersistentInputCompleted, EProjectInputAction::Aim);
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Canceled, this, &APlayerCharacter::HandlePersistentInputCanceled, EProjectInputAction::Aim);
		}

		if (FireAction)
		{
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &APlayerCharacter::StartFire);
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &APlayerCharacter::StopFire);
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Canceled, this, &APlayerCharacter::StopFire);
		}

		if (ReloadAction)
		{
			EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this, &APlayerCharacter::StartReload);
		}

		if (UniqueSkillAction)
		{
			EnhancedInputComponent->BindAction(UniqueSkillAction, ETriggerEvent::Started, this, &APlayerCharacter::StartUniqueSkill);
		}
	}

	// 상호작용 입력 바인딩 (IA_Interact) - 실행 대상 선택은 컴포넌트가 한다
	InteractComponent->BindInput(PlayerInputComponent, InteractAction);
}

void APlayerCharacter::Move(const FInputActionValue &Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	if (MovementVector.IsNearlyZero() || !Controller || IsMoveInputIgnored())
	{
		StopMove();
		return;
	}

	if (AbilitySystemComponent)
	{
		if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Movement_Dodging) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Groggy) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Debuff_Staggered) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Interacting) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Dead) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Reviving))
		{
			StopMove();
			return; // 구르기 중이거나 그로기, 상호작용, 사망, 부활 상태일 때는 이동(방향) 입력 무시
		}
	}

	const bool bHadMovementInput = bHasMovementInput;
	bHasMovementInput = true;

	if (Controller != nullptr)
	{
		// 컨트롤러의 Yaw 회전 값만 추출 (카메라 기준 전방 및 우측 산출)
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0.0f, Rotation.Yaw, 0.0f);

		// 카메라 기준 전방/우측 단위 벡터 계산
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// 입력 벡터에 따른 이동 적용
		AddMovementInput(ForwardDirection, MovementVector.Y);

		// 로프에서는 카메라와 캐릭터의 방향 차이와 관계없이 W/S 축만 전달한다.
		const UCharacterMovementComponent* Movement = GetCharacterMovement();
		const bool bIsRopeSwinging = Movement->MovementMode == MOVE_Custom
			&& Movement->CustomMovementMode == CMOVE_RopeSwinging;
		if (!bIsRopeSwinging)
		{
			AddMovementInput(RightDirection, MovementVector.X);
		}
	}

	if (!bHadMovementInput && bWantsToSprint)
	{
		StartSprint();
	}
}

void APlayerCharacter::StopMove()
{
	bHasMovementInput = false;
	// 홀드/토글 의도는 보존하되, 방향 입력이 끝나면 달리기 자체를 종료한다.
	CancelSprintAbility();
}

void APlayerCharacter::Look(const FInputActionValue &Value)
{
	if (AbilitySystemComponent)
	{
		if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Interacting) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Dead) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Reviving))
		{
			return; // 상호작용, 사망, 부활 중에는 카메라 회전 무시
		}
	}

	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// 마우스/스틱 시점 회전값 컨트롤러에 적용
		const UProjectInputSettingsSubsystem* Settings = InputSettings.Get();
		const float Sensitivity = Settings
			? (bIsAiming ? Settings->GetAimSensitivity() : Settings->GetLookSensitivity()) : 1.0f;
		AddControllerYawInput(LookAxisVector.X * Sensitivity);
		AddControllerPitchInput(LookAxisVector.Y * Sensitivity);
	}
}

void APlayerCharacter::ApplyWeaponRecoil(float RecoilPitch, float RecoilYawRatio)
{
	const FVector2D RecoilKick = UWeaponRecoilLibrary::CalculateRecoilKick(RecoilPitch, RecoilYawRatio);
	if (RecoilKick.IsNearlyZero())
	{
		return;
	}

	if (IsLocallyControlled())
	{
		ApplyWeaponRecoilLocal(RecoilKick.X, RecoilKick.Y);
		return;
	}

	if (HasAuthority())
	{
		ClientApplyWeaponRecoil(RecoilKick.X, RecoilKick.Y);
	}
}

void APlayerCharacter::ClientApplyWeaponRecoil_Implementation(float PitchKick, float YawKick)
{
	ApplyWeaponRecoilLocal(PitchKick, YawKick);
}

void APlayerCharacter::ClientNotifyDamageTaken_Implementation(float DamageAmount)
{
	if (DamageAmount > 0.0f)
	{
		OnDamageTaken.Broadcast(DamageAmount);
	}
}

void APlayerCharacter::ApplyWeaponRecoilLocal(float PitchKick, float YawKick)
{
	if (!Controller)
	{
		return;
	}

	AddControllerPitchInput(PitchKick);
	AddControllerYawInput(YawKick);

	PendingRecoilRecoveryPitch += -PitchKick;
	PendingRecoilRecoveryYaw += -YawKick;
}

void APlayerCharacter::RecoverWeaponRecoil(float DeltaTime)
{
	if (!Controller)
	{
		return;
	}

	if (FMath::IsNearlyZero(PendingRecoilRecoveryPitch) && FMath::IsNearlyZero(PendingRecoilRecoveryYaw))
	{
		return;
	}

	const FVector2D PendingRecovery(PendingRecoilRecoveryPitch, PendingRecoilRecoveryYaw);
	const FVector2D RecoveryStep = UWeaponRecoilLibrary::CalculateRecoveryStep(PendingRecovery, RecoilRecoverySpeed, DeltaTime);

	AddControllerPitchInput(RecoveryStep.X);
	AddControllerYawInput(RecoveryStep.Y);

	const FVector2D RemainingRecovery = UWeaponRecoilLibrary::SnapSmallRecovery(PendingRecovery - RecoveryStep);
	PendingRecoilRecoveryPitch = RemainingRecovery.X;
	PendingRecoilRecoveryYaw = RemainingRecovery.Y;
}

void APlayerCharacter::BindInputSettings()
{
	if (!IsLocallyControlled())
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UProjectInputSettingsSubsystem* Settings = GameInstance
		? GameInstance->GetSubsystem<UProjectInputSettingsSubsystem>() : nullptr;
	if (InputSettings.Get() != Settings)
	{
		if (UProjectInputSettingsSubsystem* Previous = InputSettings.Get())
		{
			Previous->OnInputModeChanged.Remove(InputModeChangedHandle);
		}
		InputModeChangedHandle.Reset();
		InputSettings = Settings;
		if (Settings)
		{
			InputModeChangedHandle = Settings->OnInputModeChanged.AddUObject(this, &ThisClass::OnInputModeChanged);
		}
	}
	if (InputAbilitySystem.Get() != AbilitySystemComponent)
	{
		if (UAbilitySystemComponent* Previous = InputAbilitySystem.Get())
		{
			Previous->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetStaminaAttribute())
				.Remove(InputStaminaChangedHandle);
		}
		InputStaminaChangedHandle.Reset();
		InputAbilitySystem = AbilitySystemComponent;
		if (AbilitySystemComponent)
		{
			InputStaminaChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetStaminaAttribute())
				.AddUObject(this, &ThisClass::OnInputStaminaChanged);
		}
	}
	if (WeaponComponent)
	{
		WeaponComponent->OnWeaponEquipped.AddUniqueDynamic(this, &ThisClass::OnInputWeaponEquipped);
	}
}

void APlayerCharacter::UnbindInputSettings()
{
	if (UProjectInputSettingsSubsystem* Settings = InputSettings.Get())
	{
		Settings->OnInputModeChanged.Remove(InputModeChangedHandle);
	}
	if (UAbilitySystemComponent* ASC = InputAbilitySystem.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetStaminaAttribute())
			.Remove(InputStaminaChangedHandle);
	}
	if (WeaponComponent)
	{
		WeaponComponent->OnWeaponEquipped.RemoveDynamic(this, &ThisClass::OnInputWeaponEquipped);
	}
	InputSettings.Reset();
	InputAbilitySystem.Reset();
	InputModeChangedHandle.Reset();
	InputStaminaChangedHandle.Reset();
}

EProjectInputMode APlayerCharacter::GetPersistentInputMode(EProjectInputAction Action) const
{
	// 스마트 트래킹은 일반 조준 설정과 관계없이 정상 키 해제에서 사격한다.
	if (Action == EProjectInputAction::Aim && bAimInputUsesSmartTracking)
	{
		return EProjectInputMode::Hold;
	}
	const UProjectInputSettingsSubsystem* Settings = InputSettings.Get();
	return Settings ? Settings->GetInputMode(Action) : EProjectInputMode::Hold;
}

void APlayerCharacter::HandlePersistentInputStarted(EProjectInputAction Action)
{
	if (!IsLocallyControlled() || HasStartedDeath())
	{
		return;
	}
	if (Action == EProjectInputAction::Aim)
	{
		bAimInputUsesSmartTracking = WeaponComponent && WeaponComponent->HasModifier(EWeaponModiferType::SmartTracking);
	}
	const bool bToggle = GetPersistentInputMode(Action) == EProjectInputMode::Toggle;
	switch (Action)
	{
	case EProjectInputAction::Sprint:
		if (bToggle && bWantsToSprint) StopSprint();
		else StartSprint();
		break;
	case EProjectInputAction::Crouch:
		if (bToggle && bWantsToCrouch) StopCrouch();
		else StartCrouch();
		break;
	case EProjectInputAction::Aim:
		if (bToggle && bWantsToAim) CancelAimInput();
		else
		{
			// 새 조준 입력은 달리기와 재개 의도를 함께 해제해 입력 큐의 재활성화를 막는다.
			StopSprint();
			StartAim();
		}
		break;
	default:
		break;
	}
}

void APlayerCharacter::HandlePersistentInputCompleted(EProjectInputAction Action)
{
	if (GetPersistentInputMode(Action) == EProjectInputMode::Toggle)
	{
		return;
	}
	if (Action == EProjectInputAction::Aim)
	{
		StopAim();
		return;
	}
	HandlePersistentInputCanceled(Action);
}

void APlayerCharacter::HandlePersistentInputCanceled(EProjectInputAction Action)
{
	switch (Action)
	{
	case EProjectInputAction::Sprint: StopSprint(); break;
	case EProjectInputAction::Crouch: StopCrouch(); break;
	case EProjectInputAction::Aim: CancelAimInput(); break;
	default: break;
	}
}

void APlayerCharacter::CancelAimInput()
{
	bWantsToAim = false;
	bAimInputUsesSmartTracking = false;
	if (!AbilitySystemComponent)
	{
		return;
	}
	// 강제 종료에서는 InputReleased를 보내지 않아 일제 사격을 방지한다.
	for (const TSubclassOf<UGameplayAbility> AbilityClass : { AimAbilityClass, SmartTrackingAbilityClass })
	{
		if (AbilityClass)
		{
			if (const FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
			{
				if (Spec->IsActive())
				{
					AbilitySystemComponent->CancelAbilityHandle(Spec->Handle);
				}
			}
		}
	}
}

void APlayerCharacter::ResetPersistentInput()
{
	// 모든 의도를 먼저 해제해야 종료 콜백이 다른 유지 동작을 재개하지 않는다.
	bWantsToSprint = false;
	bHasMovementInput = false;
	bWantsToCrouch = false;
	bWantsToAim = false;
	bIsFiring = false;
	bAimInputUsesSmartTracking = false;
	StopSprint();
	StopCrouch();
	CancelAimInput();
	StopFireInternal();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InputQueueTimerHandle);
	}
}

void APlayerCharacter::OnInputModeChanged(EProjectInputAction Action, EProjectInputMode Mode)
{
	HandlePersistentInputCanceled(Action);
}

void APlayerCharacter::OnInputWeaponEquipped(AWeaponBase* NewWeapon)
{
	CancelAimInput();
}

void APlayerCharacter::OnInputStaminaChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue <= 0.0f && GetPersistentInputMode(EProjectInputAction::Sprint) == EProjectInputMode::Toggle)
	{
		// 실제 달리기 종료는 기존 GA_Sprint가 담당하고, 여기서는 재개 의도만 해제한다.
		bWantsToSprint = false;
	}
	else if (Data.OldValue <= 0.0f && Data.NewValue > 0.0f && bWantsToSprint)
	{
		QueueInputEvaluation();
	}
}

void APlayerCharacter::QueueInputEvaluation()
{
	if (!IsLocallyControlled() || HasStartedDeath() || !(bWantsToSprint || bWantsToCrouch || bWantsToAim || bIsFiring))
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		// 한 프레임에 여러 태그가 사라져도 같은 입력 큐는 한 번만 예약한다.
		FTimerManager& Timers = World->GetTimerManager();
		if (!Timers.IsTimerActive(InputQueueTimerHandle))
		{
			InputQueueTimerHandle = Timers.SetTimerForNextTick(this, &ThisClass::EvaluateInputQueue);
		}
	}
}

void APlayerCharacter::StartSprint()
{
	if (HasStartedDeath())
	{
		return;
	}
	bWantsToSprint = true;
	// 고갈 상태의 활성화/종료 콜백이 매 프레임 큐를 다시 예약하는 것을 막는다.
	if (AbilitySystemComponent && AbilitySystemComponent->GetNumericAttribute(UCharacterAttributeSet::GetStaminaAttribute()) <= 0.0f)
	{
		if (GetPersistentInputMode(EProjectInputAction::Sprint) == EProjectInputMode::Toggle)
		{
			bWantsToSprint = false;
		}
		return;
	}
	if (!bHasMovementInput || IsMoveInputIgnored() || bIsFiring)
	{
		return;
	}
	if (AbilitySystemComponent && SprintAbilityClass)
	{
		AbilitySystemComponent->TryActivateAbilityByClass(SprintAbilityClass);
	}
}

void APlayerCharacter::StopSprint()
{
	bWantsToSprint = false;
	CancelSprintAbility();
}

void APlayerCharacter::CancelSprintAbility()
{
	if (AbilitySystemComponent && SprintAbilityClass)
	{
		if (FGameplayAbilitySpec *Spec = AbilitySystemComponent->FindAbilitySpecFromClass(SprintAbilityClass))
		{
			if (Spec->IsActive())
			{
				AbilitySystemComponent->CancelAbilityHandle(Spec->Handle);
			}
		}
	}
}

void APlayerCharacter::StartCrouch()
{
	bWantsToCrouch = true;
	if (AbilitySystemComponent && CrouchAbilityClass)
	{
		AbilitySystemComponent->TryActivateAbilityByClass(CrouchAbilityClass);
	}
}

void APlayerCharacter::StopCrouch()
{
	bWantsToCrouch = false;
	if (AbilitySystemComponent && CrouchAbilityClass)
	{
		if (FGameplayAbilitySpec *Spec = AbilitySystemComponent->FindAbilitySpecFromClass(CrouchAbilityClass))
		{
			if (Spec->IsActive())
			{
				AbilitySystemComponent->CancelAbilityHandle(Spec->Handle);
			}
		}
	}
}

void APlayerCharacter::StartAim()
{
	bWantsToAim = true;

	if (!AbilitySystemComponent)
	{
		return;
	}

	if (WeaponComponent && WeaponComponent->HasModifier(EWeaponModiferType::SmartTracking))
	{
		if (SmartTrackingAbilityClass)
		{
			AbilitySystemComponent->TryActivateAbilityByClass(SmartTrackingAbilityClass);
		}
		return;
	}

	// 줌 인 설정 등은 모두 OnAimingTagChanged(GAS 태그 콜백)에서 자동 동기화됩니다.
	if (AimAbilityClass)
	{
		AbilitySystemComponent->TryActivateAbilityByClass(AimAbilityClass);
	}
}

void APlayerCharacter::StopAim()
{
	bWantsToAim = false;
	bAimInputUsesSmartTracking = false;

	if (!AbilitySystemComponent) 
	{
		return;
	}

	if (SmartTrackingAbilityClass)
	{
		if (FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(SmartTrackingAbilityClass))
		{
			if (Spec->IsActive())
			{
				AbilitySystemComponent->AbilitySpecInputReleased(*Spec);
				return;
			}
		}
	}

	if (AimAbilityClass)
	{
		FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(AimAbilityClass);

		if (Spec && Spec->IsActive())
		{
			AbilitySystemComponent->CancelAbilityHandle(Spec->Handle);
		}
	}
}

void APlayerCharacter::StartFire()
{
	StartFireInternal();
}

void APlayerCharacter::StopFire()
{
	StopFireInternal();
}

void APlayerCharacter::StartFireInternal()
{
	bIsFiring = true;
	UpdateRotationMode(); // 사격 시작 시 즉시 회전 모드 갱신

	// Fire is blocked while the sprint state tag is active. End sprint first so
	// the initial fire press is not lost and held fire can start immediately.
	if (AbilitySystemComponent && SprintAbilityClass)
	{
		if (FGameplayAbilitySpec* SprintSpec = AbilitySystemComponent->FindAbilitySpecFromClass(SprintAbilityClass))
		{
			if (SprintSpec->IsActive())
			{
				AbilitySystemComponent->CancelAbilityHandle(SprintSpec->Handle);
			}
		}
	}

	// 스마트 트래킹 중에는 일반 사격 어빌리티가 돌면 안 된다.
	if (AbilitySystemComponent && FireAbilityClass
		&& !AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Weapon_SmartTracking))
	{
		if (FGameplayAbilitySpec *Spec = AbilitySystemComponent->FindAbilitySpecFromClass(FireAbilityClass))
		{
			AbilitySystemComponent->AbilitySpecInputPressed(*Spec);
		}
		AbilitySystemComponent->TryActivateAbilityByClass(FireAbilityClass);
	}
}

void APlayerCharacter::StopFireInternal()
{
	bIsFiring = false;
	UpdateRotationMode(); // 사격 종료 시 회전 모드 갱신

	if (AbilitySystemComponent && FireAbilityClass)
	{
		if (FGameplayAbilitySpec *Spec = AbilitySystemComponent->FindAbilitySpecFromClass(FireAbilityClass))
		{
			AbilitySystemComponent->AbilitySpecInputReleased(*Spec);
			if (Spec->IsActive())
			{
				AbilitySystemComponent->CancelAbilityHandle(Spec->Handle);
			}
		}
	}
	QueueInputEvaluation();
}

void APlayerCharacter::StartReload()
{
	if (AbilitySystemComponent && ReloadAbilityClass
		&& !AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Weapon_SmartTracking))
	{
		if (FGameplayAbilitySpec *Spec = AbilitySystemComponent->FindAbilitySpecFromClass(ReloadAbilityClass))
		{
			AbilitySystemComponent->AbilitySpecInputPressed(*Spec);
		}
		AbilitySystemComponent->TryActivateAbilityByClass(ReloadAbilityClass);
	}
}

void APlayerCharacter::StartUniqueSkill()
{
	if (AbilitySystemComponent)
	{
		FGameplayTagContainer SkillTags;
		SkillTags.AddTag(GameplayTagSystem::Ability_Skill_Main);
		AbilitySystemComponent->TryActivateAbilitiesByTag(SkillTags);
	}
}

void APlayerCharacter::StartDodge()
{
	if (UEnvironmentCharacterMovementComponent* EnvMovement = Cast<UEnvironmentCharacterMovementComponent>(GetCharacterMovement()))
	{
		if (EnvMovement->MovementMode == MOVE_Custom && EnvMovement->CustomMovementMode == CMOVE_RopeSwinging)
		{
			EnvMovement->DismountRope();
			return;
		}
	}

	if (AbilitySystemComponent && DodgeAbilityClass)
	{
		if (SmartTrackingAbilityClass)
		{
			if (FGameplayAbilitySpec* SmartTrackingSpec = AbilitySystemComponent->FindAbilitySpecFromClass(SmartTrackingAbilityClass))
			{
				if (SmartTrackingSpec->IsActive())
				{
					AbilitySystemComponent->CancelAbilityHandle(SmartTrackingSpec->Handle);
				}
			}
		}

		// 클라이언트와 서버 간의 구르기 방향 완벽 동기화를 위해 방향 명시적 추출
		FVector InputDir = GetCharacterMovement()->GetLastInputVector();
		if (InputDir.IsNearlyZero())
		{
			FRotator ControlRot = GetControlRotation();
			InputDir = ControlRot.Vector();
			InputDir.Z = 0.0f;
			InputDir.Normalize();
		}

		// 로컬 변수 저장 및 서버로 전송 (RPC 순서 보장됨)
		ReplicatedDodgeDirection = InputDir;
		if (!HasAuthority())
		{
			ServerSetDodgeDirection(InputDir);
		}

		AbilitySystemComponent->TryActivateAbilityByClass(DodgeAbilityClass);
	}
}

void APlayerCharacter::ServerSetDodgeDirection_Implementation(FVector InDirection)
{
	ReplicatedDodgeDirection = InDirection;
}

void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 서버에서 클라이언트(또는 자신)의 카메라 좌우 회전값을 가져와 복제합니다.
	// 이 값이 있어야 다른 클라이언트(Simulated Proxy)들이 에임 오프셋(AO) 좌우를 볼 수 있습니다.
	if (HasAuthority())
	{
		ReplicatedControlYaw = GetControlRotation().Yaw;
	}

	// 매 프레임 캐릭터의 이동 가속도 상태를 체크하여 카메라 스트레이프 모드를 자동으로 동기화합니다.
	// 가속도(Acceleration)는 서버와 클라이언트, Simulated Proxy 모두에게 자동으로 복제되므로 완벽하게 동기화됩니다.
	UpdateRotationMode();

	if (IsLocallyControlled())
	{
		RecoverWeaponRecoil(DeltaTime);

		if (FollowCamera)
		{
			const float CurrentDistance = (FollowCamera->GetComponentLocation() - GetActorLocation()).Size();
			const float CutoffDistance = 50.0f; // 완전 은폐 임계값

			const bool bShouldHideOwner = (CurrentDistance <= CutoffDistance);

			if (GetMesh()->bOwnerNoSee != bShouldHideOwner)
			{
				GetMesh()->SetOwnerNoSee(bShouldHideOwner);

				if (UWeaponComponent* WeaponComp = FindComponentByClass<UWeaponComponent>())
				{
					if (AWeaponBase* Weapon = WeaponComp->GetEquippedWeapon())
					{
						TArray<UMeshComponent*> WeaponMeshes;
						Weapon->GetComponents<UMeshComponent>(WeaponMeshes);
						for (UMeshComponent* WMesh : WeaponMeshes)
						{
							WMesh->SetOwnerNoSee(bShouldHideOwner);
						}
					}
				}
			}
		}
	}
}

void APlayerCharacter::UpdateRotationMode()
{
	if (AbilitySystemComponent)
	{
		if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Movement_Dodging) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Groggy) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Debuff_Staggered) ||
			AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_Interacting))
		{
			return; // 구르기 중이거나 그로기, 상호작용 상태일 때는 회전 모드를 변경하지 않음
		}
	}

	// 가속도(Acceleration)는 서버에서 Tick 도중에 0으로 초기화될 수 있어 동기화가 끊깁니다.
	// 따라서 항상 유지되는 Velocity(속도)를 사용하여 이동 상태를 확실히 판별합니다.
	bool bIsMoving = GetVelocity().SizeSquared2D() > 10.0f;

	// 조준 중, 이동 중, 또는 사격 중일 때 -> 카메라 방향으로 캐릭터 부드럽게 고정 (스트레이프)
	bool bShouldFaceCamera = bIsAiming || bIsMoving || bIsFiring;

	if (bShouldFaceCamera)
	{
		GetCharacterMovement()->bUseControllerDesiredRotation = true;
		GetCharacterMovement()->bOrientRotationToMovement = false;
	}
	else
	{
		GetCharacterMovement()->bUseControllerDesiredRotation = false;
		GetCharacterMovement()->bOrientRotationToMovement = true;
	}
}

void APlayerCharacter::OnAimingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	bool bWasAiming = bIsAiming;
	bIsAiming = (NewCount > 0);

	// 카메라 조작이나 회전 제어는 실제 조종 중인 클라이언트/서버에서만 처리
	if (bWasAiming != bIsAiming && IsLocallyControlled())
	{
		TargetFOV = bIsAiming ? AimFOV : DefaultFOV;
		GetWorld()->GetTimerManager().SetTimer(ZoomTimerHandle, this, &APlayerCharacter::UpdateCameraZoom, 0.016f, true);
		UpdateRotationMode();
	}
}

void APlayerCharacter::OnInputBlockingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (Tag == GameplayTagSystem::State_Movement_Sprinting && NewCount > 0 && IsLocallyControlled())
	{
		// 달리기는 기존 조준 의도까지 취소하여 달리기 종료 후 자동 조준을 막는다.
		CancelAimInput();
	}
	if (NewCount == 0)
	{
		// 태그가 제거되는 시점은 아직 EndAbility 내부이므로 다음 프레임에 재평가한다.
		QueueInputEvaluation();
	}
}

void APlayerCharacter::EvaluateInputQueue()
{
	InputQueueTimerHandle.Invalidate();
	if (!IsLocallyControlled() || HasStartedDeath())
	{
		return;
	}
	// 유지 의도만 재실행한다. 키 입력의 토글 반전은 이 경로에서 하지 않는다.
	if (bWantsToSprint && !bIsFiring)
		StartSprint();
	if (bWantsToAim)
		StartAim();
	if (bWantsToCrouch)
		StartCrouch();
	if (bIsFiring)
		StartFireInternal();
}

void APlayerCharacter::UpdateCameraZoom()
{
	if (!FollowCamera)
		return;

	const float CurrentFOV = FollowCamera->FieldOfView;
	// 0.016f (약 60프레임 속도)로 설정된 속도로 부드럽게 보간
	const float NewFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, 0.016f, ZoomInterpSpeed);

	FollowCamera->SetFieldOfView(NewFOV);

	// 목표 FOV에 거의 도달했다면 타이머 종료 (최적화)
	if (FMath::IsNearlyEqual(CurrentFOV, TargetFOV, 0.1f))
	{
		FollowCamera->SetFieldOfView(TargetFOV);
		GetWorld()->GetTimerManager().ClearTimer(ZoomTimerHandle);
	}
}



void APlayerCharacter::BeginDeath()
{
	ResetPersistentInput();
	Super::BeginDeath();

	if (CameraBoom && IsLocallyControlled())
	{
		// 60프레임 간격(약 0.016초)으로 서서히 타겟 암 길이를 늘립니다.
		GetWorldTimerManager().SetTimer(TH_CameraZoom, FTimerDelegate::CreateLambda([this]()
		{
			if (IsValid(this) && CameraBoom)
			{
				CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, 300.0f, 0.016f, 2.0f);
				
				// 목표 길이(1000)에 도달하면 타이머 종료
				if (FMath::IsNearlyEqual(CameraBoom->TargetArmLength, 300.0f, 1.0f))
				{
					GetWorldTimerManager().ClearTimer(TH_CameraZoom);
				}
			}
		}), 0.016f, true);
	}

}

void APlayerCharacter::FinishDeath()
{
	Super::FinishDeath();
}


void APlayerCharacter::Revive()
{
	Super::Revive();

	if (CameraBoom && IsLocallyControlled())
	{
		GetWorldTimerManager().ClearTimer(TH_CameraZoom);
		CameraBoom->TargetArmLength = 140.0f; // 기본 줌 길이로 즉시 원상복구
	}
}

void APlayerCharacter::GetAimOffsetAngles(float &OutPitch, float &OutYaw) const
{
	if (!FollowCamera || !GetMesh())
	{
		OutPitch = 0.0f;
		OutYaw = 0.0f;
		return;
	}

	bool bIsSprinting = false;
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		bIsSprinting = ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Movement_Sprinting);
	}

	float DeltaTime = GetWorld()->GetDeltaSeconds();
	float InterpSpeed = 25.0f; // 값이 클수록 에임이 타겟에 빠르게 달라붙음

	if (bIsSprinting)
	{
		// 달리는 도중에는 에임 오프셋을 사용하지 않으므로, 누적된 오차값을 부드럽게 0으로 리셋하여 
		// 달리기 종료 시 값이 튀는 현상(Integral Windup)을 방지합니다.
		CurrentAimPitch = FMath::FInterpTo(CurrentAimPitch, 0.0f, DeltaTime, InterpSpeed);
		CurrentAimYaw = FMath::FInterpTo(CurrentAimYaw, 0.0f, DeltaTime, InterpSpeed);
		CurrentAimYawCorrection = 0.0f;
		AimYawCorrectionFrame = MAX_uint64;
	}
	else
	{
		// 1. 카메라가 정확히 바라보는 실제 목표 지점 계산
		FVector CameraLocation = FollowCamera->GetComponentLocation();
		FVector CameraForward = FollowCamera->GetForwardVector();
		FVector TraceEnd = CameraLocation + (CameraForward * AimTraceDistance);

		// 기본 타겟 포인트는 허공
		FVector TargetPoint = TraceEnd;

		// [라인 트레이스 적용] 카메라 정중앙에서 레이저를 쏴서 벽이나 오브젝트에 닿는지 검사
		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this); // 자기 자신은 예외

		bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, CameraLocation, TraceEnd, ECC_Visibility, QueryParams);
		if (bHit)
		{
			float HitDistance = (HitResult.ImpactPoint - CameraLocation).Size();
			
			// 에임 오프셋(상체 굽힘)에 의해 변동되지 않는 안정적인 '캐릭터 중심'을 기준으로 최소 거리 계산
			// 카메라에서 캐릭터까지의 거리 + 총신이 튀어나온 넉넉한 여유 거리(150.0f)
			float CameraToActorDist = (GetActorLocation() - CameraLocation).Size();
			float MinDistance = CameraToActorDist + 150.0f; 

			// 타겟이 너무 가까워서 총구보다 뒤에 위치하게 되거나,
			// 총구가 움직일 때마다 각도가 180도씩 뒤집히는 무한 피드백(요동침) 루프가 발생하지 않도록 억제합니다.
			if (HitDistance < MinDistance)
			{
				// 최소 거리 밖(벽 너머나 땅바닥 밑)으로 가상의 타겟을 밀어넣어 안정적인 각도를 유지합니다.
				TargetPoint = CameraLocation + (CameraForward * MinDistance);
			}
			else
			{
				TargetPoint = HitResult.ImpactPoint;
			}
		}

		// 2. 사격의 기준점이 되는 내 몸(AimSocketName)과 실제 총구(Muzzle) 위치 및 회전 획득
		FVector AimSocketLocation = GetMesh()->GetSocketLocation(AimSocketName);
		FVector MuzzleLocation = AimSocketLocation + GetActorForwardVector() * 50.0f; // 기본값
		FRotator MuzzleRotation = GetActorRotation(); // 기본 총구 방향
		bool bHasValidMuzzle = false;

		if (UWeaponComponent *WeaponComp = FindComponentByClass<UWeaponComponent>())
		{
			if (AWeaponBase *Weapon = WeaponComp->GetEquippedWeapon())
			{
				if (UStaticMeshComponent *FirePointMesh = Weapon->GetFirePointMeshComponent())
				{
					MuzzleLocation = FirePointMesh->GetSocketLocation(Weapon->GetFirePointSocketName());
					MuzzleRotation = FirePointMesh->GetSocketRotation(Weapon->GetFirePointSocketName());
					bHasValidMuzzle = FirePointMesh->DoesSocketExist(Weapon->GetFirePointSocketName());
				}
			}
		}

		// 3. 기하학적 오차(Error) 계산 (실제 총구가 향하는 방향 vs 총구에서 타겟을 바라보는 방향)
		FRotator CurrentGunDir = MuzzleRotation;
		FRotator DesiredDir = (TargetPoint - MuzzleLocation).Rotation();

		// 카메라를 수직(아래/위)으로 꽂았을 때, 카메라의 시점은 캐릭터의 중심바닥을 보게되는데 총구는 아무리 내려도 몸을 뚫고 중심바닥을 볼 수 없음
		// 하지만 오프셋은 총구와 카메라의 정렬을 맞추기 위해 요동침 -> Yaw의 과한 각도를 보정함
		float YawDiff = FMath::FindDeltaAngleDegrees(CameraForward.Rotation().Yaw, DesiredDir.Yaw);
		if (FMath::Abs(YawDiff) > 80.0f)
		{
			// 억지로 뒤를 돌아보려 하지 말고, 카메라가 바라보는 방향의 Yaw를 그대로 따르도록 강제 보정합니다.
			DesiredDir.Yaw = CameraForward.Rotation().Yaw;
		}

		FRotator Error = (DesiredDir - CurrentGunDir);
		Error.Normalize();

		// 오차 크기에 따른 동적 보간 속도 매핑
		float ErrorMagnitude = FMath::Max(FMath::Abs(Error.Pitch), FMath::Abs(Error.Yaw));
		float DynamicSpeed = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 180.0f), FVector2D(5.0f, 15.0f), ErrorMagnitude);

		// 몸 기준 상대각은 즉시 반영하고 총구 정밀 오차만 별도 보정값에 누적한다.
		CurrentAimPitch += Error.Pitch * DynamicSpeed * DeltaTime;
		const float BaseAimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, GetBaseAimRotation().Yaw), -90.0f, 90.0f);
		const float ProposedAimYaw = FMath::Clamp(BaseAimYaw + CurrentAimYawCorrection, -90.0f, 90.0f);
		// 읽은 총구는 아직 이전 AO 포즈다. 이번 기본각 변경의 예상 효과를 빼 이중 보정을 막는다.
		const float PredictedPoseYawDelta = ProposedAimYaw - CurrentAimYaw;
		const float CorrectionErrorYaw = FRotator::NormalizeAxis(Error.Yaw - PredictedPoseYawDelta);
		if (AimYawCorrectionFrame != GFrameCounter)
		{
			if (AimYawCorrectionFrame == MAX_uint64 || !bHasValidMuzzle)
			{
				// 최초 갱신과 무기 소켓이 없는 경우에는 기본 상대각부터 적용한다.
				CurrentAimYawCorrection = 0.0f;
			}
			else
			{
				const float CorrectionAlpha = FMath::Clamp(DynamicSpeed * DeltaTime, 0.0f, 1.0f);
				CurrentAimYawCorrection += CorrectionErrorYaw * CorrectionAlpha;
			}
			// 최종 AO 범위를 넘는 보정은 저장하지 않아 제한각에서 오차가 계속 쌓이지 않도록 한다.
			CurrentAimYawCorrection = FMath::Clamp(CurrentAimYawCorrection, -90.0f - BaseAimYaw, 90.0f - BaseAimYaw);
			AimYawCorrectionFrame = GFrameCounter;
		}
		CurrentAimYaw = BaseAimYaw + CurrentAimYawCorrection;

		// 몸이 기괴하게 꺾이지 않도록 AimOffset의 물리적 한계치 제한 
		CurrentAimPitch = FMath::Clamp(CurrentAimPitch, -90.0f, 90.0f);
		CurrentAimYaw = FMath::Clamp(CurrentAimYaw, -90.0f, 90.0f);
	}

	// 최종 출력
	OutPitch = CurrentAimPitch;
	OutYaw = CurrentAimYaw;
}

bool APlayerCharacter::GetMainSkillCooldown(float& OutRemaining, float& OutDuration) const
{
	OutRemaining = 0.0f;
	OutDuration = 0.0f;

	if (!AbilitySystemComponent)
	{
		return false;
	}

	//쿨다운 남은 시간은 ASC가 들고 있다. 캐릭터가 따로 캐싱하면 출처가 둘이 된다.
	//ABILITYLIST_SCOPE_LOCK 매크로는 *this를 쓰므로 ASC 밖에서는 직접 잠근다
	FScopedAbilityListLock ActiveScopeLock(*AbilitySystemComponent);
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		if (!Spec.Ability || !Spec.Ability->GetAssetTags().HasTag(GameplayTagSystem::Ability_Skill_Main))
		{
			continue;
		}

		Spec.Ability->GetCooldownTimeRemainingAndDuration(
			Spec.Handle,
			AbilitySystemComponent->AbilityActorInfo.Get(),
			OutRemaining,
			OutDuration);
		return OutDuration > 0.0f;
	}

	return false;
}

bool APlayerCharacter::GetSkillDurationRemaining(
	const FGameplayTag DurationTag,
	float& OutRemaining,
	float& OutDuration) const
{
	OutRemaining = 0.0f;
	OutDuration = 0.0f;

	if (!AbilitySystemComponent || !DurationTag.IsValid())
	{
		return false;
	}

	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(
		FGameplayTagContainer(DurationTag));

	for (const TPair<float, float>& TimePair : AbilitySystemComponent->GetActiveEffectsTimeRemainingAndDuration(Query))
	{
		OutRemaining = TimePair.Key;
		OutDuration = TimePair.Value;
		return OutDuration > 0.0f;
	}

	return false;
}

void APlayerCharacter::SetRopeCameraMode(bool bIsSwinging)
{
	if (!IsLocallyControlled() || !CameraBoom)
	{
		return;
	}

	TargetRopeArmLength = bIsSwinging ? RopeTargetArmLength : DefaultTargetArmLength;
	GetWorld()->GetTimerManager().SetTimer(RopeCameraTimerHandle, this, &APlayerCharacter::UpdateRopeCameraOffset, 0.016f, true);
}

void APlayerCharacter::UpdateRopeCameraOffset()
{
	if (!CameraBoom)
	{
		return;
	}

	const float CurrentLength = CameraBoom->TargetArmLength;
	const float NewLength = FMath::FInterpTo(CurrentLength, TargetRopeArmLength, 0.016f, ArmLengthInterpSpeed);

	CameraBoom->TargetArmLength = NewLength;

	if (FMath::IsNearlyEqual(CurrentLength, TargetRopeArmLength, 0.5f))
	{
		CameraBoom->TargetArmLength = TargetRopeArmLength;
		GetWorld()->GetTimerManager().ClearTimer(RopeCameraTimerHandle);
	}
}

void APlayerCharacter::SetWeaponHidden(bool bHideWeapon)
{
	if (UWeaponComponent* WeaponComp = FindComponentByClass<UWeaponComponent>())
	{
		if (AWeaponBase* Weapon = WeaponComp->GetEquippedWeapon())
		{
			TArray<UMeshComponent*> WeaponMeshes;
			Weapon->GetComponents<UMeshComponent>(WeaponMeshes);
			for (UMeshComponent* WMesh : WeaponMeshes)
			{
				// 로프 탑승 시 총기 렌더링을 완전히 끔
				WMesh->SetHiddenInGame(bHideWeapon, true);
			}
		}
	}
}
