#include "MonsterCharacter.h"
#include "GameCore/AI/MonsterAIController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameCore/Movement/GroundMonsterMovementComponent.h"
#include "MotionWarpingComponent.h"
#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "GameCore/Combat/MonsterDropComponent.h"
#include "GameCore/Attribute/CharacterAttributeSet.h"
#include "GameCore/Attribute/MonsterInitData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameCore/UI/MonsterHealthBarVisibilitySubsystem.h"
#include "GameCore/UI/MonsterStatusWidget.h"
#include "Net/UnrealNetwork.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/GameStateBase.h"

AMonsterCharacter::AMonsterCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	CharacterRoleTag = GameplayTagSystem::Character_Enemy;
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));
	DropComponent = CreateDefaultSubobject<UMonsterDropComponent>(TEXT("DropComponent"));
	MonsterHealthBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("MonsterHealthBarComponent"));
	MonsterHealthBarComponent->SetupAttachment(GetMesh());
	MonsterHealthBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
	MonsterHealthBarComponent->SetDrawAtDesiredSize(true);
	MonsterHealthBarComponent->SetPivot(FVector2D(0.5f, 0.5f));
	MonsterHealthBarComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 180.0f));

	// 몬스터 피격 채널 설정
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionResponseToChannel(ECC_TraceAttack, ECR_Block);
	}
}

void AMonsterCharacter::BeginPlay()
{
	Super::BeginPlay();
	ApplyMonsterInitialStats();
	InitializeMonsterHealthBar();

	// 레벨에 미리 배치된 몬스터는 인카운터 볼륨에 들어오기 전까지 대기한다.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(GameplayTagSystem::State_AI_Dormant);
	}

	UE_LOG(LogTemp, Warning, TEXT("[MonsterEncounter] DormantInitialized | Monster=%s | ASC=%s"), *GetNameSafe(this),
	       GetAbilitySystemComponent() ? TEXT("Valid") : TEXT("Invalid"));

	/*
	 * 휴면 초기화는 서버가 정한다. 클라이언트는 레벨 배치 액터의 복제 값이 BeginPlay보다 먼저
	 * 도착할 수 있어, 여기서 무조건 숨기면 이미 받은 "보이기"를 덮어써 계속 숨겨진 채로 남는다.
	 * 클라이언트는 지금까지 받은 복제 값을 그대로 적용하고, 이후 변화는 OnRep 가 반영한다.
	 */
	ApplyEncounterPresentation(HasAuthority() ? false : bEncounterPresentationVisible);

	if (AMonsterAIController* AC = Cast<AMonsterAIController>(GetController()))
	{
		AC->SetMovementLocked(true);
	}

	UE_LOG(
	    LogTemp, Warning,
	    TEXT("[MonsterEncounter] BeginPlayState | Monster=%s | Hidden=%s | Collision=%s | Controller=%s | Dormant=%s"),
	    *GetNameSafe(this), IsHidden() ? TEXT("True") : TEXT("False"),
	    GetActorEnableCollision() ? TEXT("Enabled") : TEXT("Disabled"), *GetNameSafe(GetController()),
	    GetAbilitySystemComponent() &&
	            GetAbilitySystemComponent()->HasMatchingGameplayTag(GameplayTagSystem::State_AI_Dormant)
	        ? TEXT("True")
	        : TEXT("False"));
}

void AMonsterCharacter::ApplyMonsterInitialStats()
{
	ApplyMonsterInitialStatsFromRow(MonsterStatsRow);
}

float AMonsterCharacter::GetMaxHealthScale() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!MaxHealthScaleByPlayerCount || !GameState)
	{
		return 1.0f;
	}

	const int32 PlayerCount = FMath::Max(1, GameState->PlayerArray.Num());
	return MaxHealthScaleByPlayerCount->GetFloatValue(static_cast<float>(PlayerCount));
}

void AMonsterCharacter::ApplyMonsterInitialStatsFromRow(const FDataTableRowHandle& StatsRow)
{
	if (!HasAuthority() || !GetAbilitySystemComponent())
	{
		return;
	}

	const FMonsterInitStats* Stats = StatsRow.GetRow<FMonsterInitStats>(GetName());
	if (!Stats)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MonsterStats] Missing row | Monster=%s | Row=%s"), *GetNameSafe(this),
		       *StatsRow.RowName.ToString());
		return;
	}

	UCharacterAttributeSet* Attributes = GetCharacterAttributeSet();
	if (!Attributes)
	{
		return;
	}

	// 몬스터 초기 스탯은 DT 값을 서버의 AttributeSet에 직접 반영한다.
	// MaxHealth를 먼저 적용한 뒤 Health를 같은 값으로 채워 스폰 시 완전 회복 상태로 시작한다.
	const float ScaledMaxHealth = Stats->MaxHealth * GetMaxHealthScale();
	Attributes->SetMaxHealth(ScaledMaxHealth);
	Attributes->SetHealth(ScaledMaxHealth);
	Attributes->SetMovementSpeed(Stats->MovementSpeed);
	Attributes->SetArmor(Stats->Armor);
	Attributes->SetAttackPower(Stats->AttackPower);
	WeakspotMultiplier = Stats->WeakspotMultiplier;
	StaggerResistance = Stats->StaggerResistance;

	UE_LOG(
	    LogTemp, Log,
	    TEXT(
	        "[MonsterStats] Applied | Monster=%s | Row=%s | Health=%.0f | Speed=%.0f | Armor=%.2f | AttackPower=%.2f | Weakspot=%.2f | StaggerResistance=%.2f"),
	    *GetNameSafe(this), *StatsRow.RowName.ToString(), ScaledMaxHealth, Stats->MovementSpeed, Stats->Armor,
	    Stats->AttackPower, WeakspotMultiplier, StaggerResistance);
}

void AMonsterCharacter::ConfigureBattleEmerge(EEmergeSpawnType InEmergeSpawnType, UAnimMontage* InEmergeMontage,
                                              bool bInUseTurnSection, bool bInUseTurnRotationWarp)
{
	EmergeSpawnType = InEmergeSpawnType;
	EmergeMontageOverride = InEmergeMontage;
	bUseEmergeTurnSection = bInUseTurnSection;
	bUseEmergeTurnRotationWarp = bInUseTurnRotationWarp;
}

void AMonsterCharacter::NotifyEmergeFinished()
{
	OnEmergeFinished.Broadcast(this);
}

void AMonsterCharacter::ReleaseBattleCombat()
{
	if (!bWaitForBattleCombatRelease)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		FGameplayEventData EventData;
		EventData.EventTag = GameplayTagSystem::Event_AI_Emerge_CombatRelease;
		EventData.Instigator = this;
		EventData.Target = this;
		ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
	}
}

bool AMonsterCharacter::RequestEncounter_Implementation(AActor* EncounterInstigator)
{
	if (!IsValid(EncounterInstigator))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MonsterEncounter] Rejected | Monster=%s | Reason=InstigatorInvalid"),
		       *GetNameSafe(this));
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	AMonsterAIController* AC = Cast<AMonsterAIController>(GetController());

	UE_LOG(
	    LogTemp, Warning,
	    TEXT(
	        "[MonsterEncounter] RequestReceived | Monster=%s | Instigator=%s | ASC=%s | Controller=%s | Dormant=%s | Dead=%s"),
	    *GetNameSafe(this), *GetNameSafe(EncounterInstigator), IsValid(ASC) ? TEXT("Valid") : TEXT("Invalid"),
	    *GetNameSafe(AC),
	    IsValid(ASC) && ASC->HasMatchingGameplayTag(GameplayTagSystem::State_AI_Dormant) ? TEXT("True") : TEXT("False"),
	    IsValid(ASC) && ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Dead) ? TEXT("True") : TEXT("False"));

	if (!IsValid(ASC) || !IsValid(AC))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MonsterEncounter] Rejected | Monster=%s | Reason=ASCOrControllerInvalid"),
		       *GetNameSafe(this));
		return false;
	}

	if (!ASC->HasMatchingGameplayTag(GameplayTagSystem::State_AI_Dormant) ||
	    ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Dead))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MonsterEncounter] Rejected | Monster=%s | Dormant=%s | Dead=%s"),
		       *GetNameSafe(this),
		       ASC->HasMatchingGameplayTag(GameplayTagSystem::State_AI_Dormant) ? TEXT("True") : TEXT("False"),
		       ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Dead) ? TEXT("True") : TEXT("False"));
		return false;
	}

	ASC->RemoveLooseGameplayTag(GameplayTagSystem::State_AI_Dormant);

	/*
	 * 프레젠테이션을 공개하면 같은 프레임부터 무기 트레이스에 맞을 수 있다.
	 * StateTree 이벤트가 GA_Emerge를 활성화하기 전까지 생기는 공백을 막고,
	 * GA가 끝날 때 이 코드가 추가한 카운트만 정확히 반환한다.
	 */
	if (!bHasPendingEmergeInvincibility)
	{
		ASC->AddLooseGameplayTag(GameplayTagSystem::State_Invincible);
		bHasPendingEmergeInvincibility = true;
	}

	bEncounterPresentationVisible = true;
	MulticastSetEncounterPresentation(true);
	AC->SetMovementLocked(false);
	AC->RequestEncounter(EncounterInstigator);
	UE_LOG(LogTemp, Warning, TEXT("[MonsterEncounter] Started | Monster=%s | Instigator=%s"), *GetNameSafe(this),
	       *GetNameSafe(EncounterInstigator));
	return true;
}

void AMonsterCharacter::ReleasePendingEmergeInvincibility()
{
	if (!bHasPendingEmergeInvincibility)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(GameplayTagSystem::State_Invincible);
	}

	bHasPendingEmergeInvincibility = false;
}

void AMonsterCharacter::SetEncounterPresentationVisibleForFrameworkTest(const bool bVisible)
{
	bEncounterPresentationVisible = bVisible;
	if (HasAuthority())
	{
		MulticastSetEncounterPresentation(bVisible);
	}
	else
	{
		ApplyEncounterPresentation(bVisible);
	}
}

void AMonsterCharacter::InitializeMonsterHealthBar()
{
	// 전용 서버는 UMG를 만들 필요가 없다. 리슨 서버와 일반 클라이언트에서는
	// 각자의 복제된 AttributeSet 값을 읽어 위젯을 독립적으로 갱신한다.
	if (GetNetMode() == NM_DedicatedServer || !bUseMonsterStatUI || !MonsterHealthBarWidgetClass)
	{
		return;
	}

	MonsterHealthBarComponent->SetWidgetClass(MonsterHealthBarWidgetClass);
	MonsterHealthBarComponent->InitWidget();

	if (UMonsterStatusWidget* HealthBarWidget =
	        Cast<UMonsterStatusWidget>(MonsterHealthBarComponent->GetUserWidgetObject()))
	{
		HealthBarWidget->InitializeForMonster(this);
	}

	if (UMonsterHealthBarVisibilitySubsystem* Visibility =
	        GetWorld()->GetSubsystem<UMonsterHealthBarVisibilitySubsystem>())
	{
		Visibility->RegisterMonster(this);
	}
}

void AMonsterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMonsterCharacter, bUseMonsterStatUI);
	DOREPLIFETIME(AMonsterCharacter, bEncounterPresentationVisible);
	DOREPLIFETIME(AMonsterCharacter, bPossessionPresentationHidden);
	DOREPLIFETIME(AMonsterCharacter, bDistanceSleeping);
}

void AMonsterCharacter::SetPossessionPresentationHidden(const bool bInHidden)
{
	bPossessionPresentationHidden = bInHidden;
	OnRep_PossessionPresentationHidden();
}

void AMonsterCharacter::OnRep_PossessionPresentationHidden()
{
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetVisibility(!bPossessionPresentationHidden, true);
	}

	if (IsValid(MonsterHealthBarComponent))
	{
		MonsterHealthBarComponent->SetVisibility(false);

		if (!bPossessionPresentationHidden)
		{
			if (UMonsterStatusWidget* HealthBarWidget =
			        Cast<UMonsterStatusWidget>(MonsterHealthBarComponent->GetUserWidgetObject()))
			{
				HealthBarWidget->ResyncFromMonster();
			}
		}
	}
}

void AMonsterCharacter::ApplyEncounterPresentation(const bool bVisible)
{
	SetActorHiddenInGame(!bVisible);
	SetActorEnableCollision(bVisible);
	bPresentationShown = bVisible;
	UpdateTickState();
}

void AMonsterCharacter::OnRep_DistanceSleeping()
{
	UpdateTickState();
}

void AMonsterCharacter::UpdateTickState()
{
	const bool bActive = bPresentationShown && !bDistanceSleeping;

	SetActorTickEnabled(bActive);
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetComponentTickEnabled(bActive);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetComponentTickEnabled(bActive);
	}
}

bool AMonsterCharacter::IsEngagedInCombat() const
{
	const AMonsterAIController* AC = Cast<AMonsterAIController>(GetController());
	return IsValid(AC) && AC->HasPerceivedTarget();
}

void AMonsterCharacter::SetDistanceSleep(const bool bSleep)
{
	if (!HasAuthority() || bDistanceSleeping == bSleep)
	{
		return;
	}

	bDistanceSleeping = bSleep;

	if (AMonsterAIController* AC = Cast<AMonsterAIController>(GetController()))
	{
		AC->SetSleeping(bSleep);
	}

	if (bSleep)
	{
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}

	UpdateTickState();

	UE_LOG(LogTemp, Log, TEXT("[MonsterSleep] %s | Monster=%s"), bSleep ? TEXT("Sleep") : TEXT("Wake"),
	       *GetNameSafe(this));
}

void AMonsterCharacter::OnRep_EncounterPresentationVisible()
{
	ApplyEncounterPresentation(bEncounterPresentationVisible);
}

void AMonsterCharacter::MulticastSetEncounterPresentation_Implementation(const bool bVisible)
{
	ApplyEncounterPresentation(bVisible);
}

void AMonsterCharacter::MulticastPlaySoundAtLocation_Implementation(USoundBase* Sound,
                                                                    const FVector_NetQuantize Location,
                                                                    const float VolumeMultiplier)
{
	if (IsValid(Sound))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, VolumeMultiplier);
	}
}

void AMonsterCharacter::AccumulateHitReactDamage(float Damage, AActor* InstigatorActor,
                                                 const FGameplayEffectContextHandle& EffectContext)
{
	// 최초 피격 시점부터 누적 시간 측정을 시작한다.
	if (!GetWorldTimerManager().IsTimerActive(AccumulatedHitReactTimerHandle))
	{
		AccumulatedHitReactDamage = 0.0f;
		AccumulatedHitReactHitCount = 0;

		GetWorldTimerManager().SetTimer(AccumulatedHitReactTimerHandle, this,
		                                &AMonsterCharacter::ResetAccumulatedHitReactDamage, AccumulatedHitReactWindow,
		                                false);
	}

	AccumulatedHitReactDamage += FMath::Max(Damage, 0.0f);
	++AccumulatedHitReactHitCount;

	bool bReachedThreshold = false;
	switch (AccumulatedHitReactCriterion)
	{
	case EAccumulatedHitReactCriterion::Damage:
		bReachedThreshold = AccumulatedHitReactDamage >= AccumulatedHitReactThreshold;
		break;

	case EAccumulatedHitReactCriterion::MaxHealthPercent:
		if (const UCharacterAttributeSet* Attributes = GetCharacterAttributeSet())
		{
			const float MaxHealth = Attributes->GetMaxHealth();
			bReachedThreshold = MaxHealth > KINDA_SMALL_NUMBER && (AccumulatedHitReactDamage / MaxHealth * 100.0f) >=
			                                                          AccumulatedHitReactMaxHealthPercentThreshold;
		}
		break;

	case EAccumulatedHitReactCriterion::HitCount:
		bReachedThreshold = AccumulatedHitReactHitCount >= AccumulatedHitReactHitCountThreshold;
		break;

	default:
		break;
	}

	if (!bReachedThreshold)
	{
		return;
	}

	const float TriggerDamage = AccumulatedHitReactDamage;

	GetWorldTimerManager().ClearTimer(AccumulatedHitReactTimerHandle);

	AccumulatedHitReactDamage = 0.0f;
	AccumulatedHitReactHitCount = 0;

	SendHitReactEvent(InstigatorActor, EffectContext, TriggerDamage);
}

void AMonsterCharacter::SendHitReactEvent(AActor* InstigatorActor, const FGameplayEffectContextHandle& EffectContext,
                                          float Damage)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	FGameplayEventData Payload;

	// 몬스터는 Heavy HitReact 및 넉백을 사용하지 않는다.
	Payload.EventTag = GameplayTagSystem::Event_HitReact;
	Payload.Instigator = InstigatorActor;
	Payload.Target = this;
	Payload.ContextHandle = EffectContext;
	Payload.EventMagnitude = Damage;

	ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
}

void AMonsterCharacter::ResetAccumulatedHitReactDamage()
{
	AccumulatedHitReactDamage = 0.0f;
	AccumulatedHitReactHitCount = 0;
}

void AMonsterCharacter::BeginDeath()
{
	Super::BeginDeath();

	if (AMonsterAIController* MonsterController = Cast<AMonsterAIController>(GetController()))
	{
		MonsterController->StopAIForDeath();
	}
}

void AMonsterCharacter::FinishDeath()
{
	// Base의 사망 완료 상태가 설정되기 전에 중복 여부를 확인한다.
	if (HasFinishedDeath())
	{
		return;
	}

	// 공통 사망 완료 처리:
	// 중복 방지 상태 설정 및 캡슐 충돌 비활성화
	Super::FinishDeath();

	// 보상 드랍과 액터 제거는 서버에서만 처리한다.
	if (HasAuthority())
	{
		// 후속 보상 시스템에 몬스터 사망을 알린다.
		OnMonsterDeath.Broadcast(this);

		// 풀 몹은 지우지 않는다. 빌려 간 스폰 포인트가 CorpseLifeSpan 뒤에 풀로 돌려준다
		if (!bPooled)
		{
			// 사망 처리가 끝난 몬스터를 CorpseLifeSpan 뒤에 제거한다. 0이면 다음 틱에 사라진다.
			SetLifeSpan(FMath::Max(CorpseLifeSpan, KINDA_SMALL_NUMBER));
		}
	}
}

void AMonsterCharacter::Revive()
{
	/*
	 * 클라는 숨김 중 메시 틱이 꺼져 사망 몽타주가 도중에 멈춘 채 남을 수 있다.
	 * 그대로 두면 다시 보일 때 몽타주가 이어 재생돼 사망 자세를 또 캡처한다. 먼저 끊는다
	 */
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		if (UAnimInstance* AnimInstance = MeshComponent->GetAnimInstance())
		{
			AnimInstance->StopAllMontages(0.0f);
		}
	}

	// 충돌·이동 복구와 사망 자세 해제는 부모가 한다
	Super::Revive();
}

//빌드용 구현==================================================

void AMonsterCharacter::Spawn()
{
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
}

void AMonsterCharacter::Hide()
{
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	// todo : 표시/충돌만 끄는중. AI는 그대로 돌아서 숨은 몬스터가 인지·공격
	// MonsterAIController에 StopAIForDeath의 짝(SetAIActive 등)이 생기면 여기서 호출
}

//============================================================

bool AMonsterCharacter::PlayEmerge(const FMonsterEmergeParams& Params, AActor* EncounterInstigator)
{
	if (!HasAuthority())
	{
		return false;
	}

	/*
	 * 휴면 태그를 단 채로 AI 를 먼저 켠다. StateTree 가 Emerge 대기(Dormant)에 들어가 있어야
	 * 아래 조우 요청이 보내는 이벤트를 받는다. 꺼져 있으면 이벤트가 그냥 사라진다
	 */
	if (AMonsterAIController* AC = Cast<AMonsterAIController>(GetController()))
	{
		AC->ResumeAI();
	}

	ConfigureBattleEmerge(Params.SpawnType, Params.Montage, Params.bUseTurnSection, Params.bUseTurnRotationWarp);
	SetWaitForBattleCombatRelease(false);

	// 휴면 해제·무적 선점·공개는 기존 조우 경로가 그대로 맡는다. 보스의 인트로 오버라이드도 여기로 이어진다
	return IEncounterTargetInterface::Execute_RequestEncounter(this, EncounterInstigator);
}

void AMonsterCharacter::StartPatrol(const TScriptInterface<IPatrolArea>& Area)
{
	if (!HasAuthority())
	{
		return;
	}

	/*
	 * 조우를 기다리지 않고 곧바로 깨운다. Encountered 는 "등장을 마친 적"이라는 뜻이라
	 * Emerge 없이 나타난 적도 같은 상태로 둔다. 타깃은 주지 않는다 — 시야에 들어와야 싸운다
	 */
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->SetLooseGameplayTagCount(GameplayTagSystem::State_AI_Dormant, 0);
		ASC->SetLooseGameplayTagCount(GameplayTagSystem::State_AI_Encountered, 1);
	}

	SetEncounterPresentationVisibleForFrameworkTest(true);

	// 태그를 바꾼 뒤에 AI 를 켠다. StateTree 가 루트에서 휴면 아님·등장 완료로 읽어 Emerge 를 건너뛰고 Idle → 순찰로 간다
	if (AMonsterAIController* AC = Cast<AMonsterAIController>(GetController()))
	{
		AC->ResumeAI();
		AC->SetMovementLocked(false);
	}
}

void AMonsterCharacter::OnAcquired(const FTransform& SpawnTransform)
{
	if (!HasAuthority())
	{
		return;
	}

	bPooled = true;
	SetLifeSpan(0.0f);

	// 보상 없이 죽었던 몹이 다시 나올 때는 드랍을 되살린다
	if (DropComponent)
	{
		DropComponent->SetDropsSuppressed(false);
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();

		// 사망 GE 가 붙인 State.Dead 를 걷는다. 태그가 빠지면 부모 콜백이 Revive 로 충돌·이동을 되살린다
		ASC->RemoveActiveEffectsWithGrantedTags(FGameplayTagContainer(GameplayTagSystem::State_Dead));
		ASC->SetLooseGameplayTagCount(GameplayTagSystem::State_Dead, 0);
		ASC->SetLooseGameplayTagCount(GameplayTagSystem::State_AI_Encountered, 0);
		ASC->SetLooseGameplayTagCount(GameplayTagSystem::State_AI_Dormant, 1);
	}

	ReleasePendingEmergeInvincibility();
	GetWorldTimerManager().ClearTimer(AccumulatedHitReactTimerHandle);
	ResetAccumulatedHitReactDamage();

	// 태그 콜백이 이미 돌렸어도 한 번 더 부르는 것은 무해하다. 처음 꺼내는 몹은 사망 이력이 없다
	if (HasStartedDeath() || HasFinishedDeath())
	{
		Revive();
	}

	// Revive 는 걷기로 되돌린다. 비행 몹은 자기 기본 이동 모드가 따로 있다
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetDefaultMovementMode();
		Movement->StopMovementImmediately();
	}

	// 분열 자식은 축소된 상태로 반환될 수 있으므로 캡슐 높이를 계산하기 전에 크기를 복원합니다.
	ResetForReuse();
	// 스폰 포인트는 바닥에 찍혀 있고 캐릭터 피벗은 캡슐 가운데다. 반 높이만큼 올려야 바닥에 박히지 않는다
	const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	SetActorLocationAndRotation(SpawnTransform.GetLocation() + FVector(0.0, 0.0, HalfHeight),
	                            SpawnTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);

	/*
	 * AI 는 여기서 켜지 않는다. 지금 켜면 모든 몹이 휴면 태그를 단 채로 StateTree 가 시작돼
	 * 순찰 몹도 Emerge 대기(Dormant)에 갇힌다. 등장 방식이 정해지는 PlayEmerge / StartPatrol 이 켠다
	 */
}

void AMonsterCharacter::OnReleased()
{
	if (!HasAuthority())
	{
		return;
	}
	if (UGroundMonsterMovementComponent* GroundMovement = Cast<UGroundMonsterMovementComponent>(GetCharacterMovement()))
	{
		GroundMovement->CancelNavLinkJump();
	}

	// 자는 중에 돌려받으면 일시정지된 StateTree 가 남는다. 먼저 깨운 뒤 멈춘다
	SetDistanceSleep(false);

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->CancelAllAbilities();
	}

	if (AMonsterAIController* AC = Cast<AMonsterAIController>(GetController()))
	{
		AC->SuspendAI(TEXT("Returned to pool"));
	}

	ReleasePendingEmergeInvincibility();
	GetWorldTimerManager().ClearTimer(AccumulatedHitReactTimerHandle);

	// 숨김·충돌 끔은 복제되어 클라에서도 사라진다
	bEncounterPresentationVisible = false;
	MulticastSetEncounterPresentation(false);

	// 충돌이 꺼진 채 중력을 받으면 끝없이 떨어진다
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
}

void AMonsterCharacter::KillWithoutReward()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!HasAuthority() || !ASC || HasStartedDeath() || ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Dead))
	{
		return;
	}

	// 골드는 데미지 라우터가 NotifyKilled 를 부를 때만 나가므로 이 경로에서는 나가지 않는다. 무기 드랍만 따로 막는다
	if (DropComponent)
	{
		DropComponent->SetDropsSuppressed(true);
	}

	ASC->ApplyModToAttribute(UCharacterAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0.0f);

	// 실제 사망과 같은 경로. GA_Death 가 어빌리티 취소와 사망 몽타주, State.Dead 를 처리한다
	FGameplayEventData Payload;
	Payload.EventTag = GameplayTagSystem::Event_Death;
	Payload.Target = this;
	ASC->HandleGameplayEvent(GameplayTagSystem::Event_Death, &Payload);
}

void AMonsterCharacter::ResetForReuse()
{
	SetUseMonsterStatUI(GetClass()->GetDefaultObject<AMonsterCharacter>()->bUseMonsterStatUI);
	ApplyMonsterInitialStats();
}

void AMonsterCharacter::NotifyDamagedBy(AActor* DamageInstigator, const float AppliedDamage)
{
	if (!HasAuthority())
	{
		return;
	}

	// 멀리서 저격당해도 자는 채로 맞지 않게 깨운다
	SetDistanceSleep(false);

	if (AMonsterAIController* AC = Cast<AMonsterAIController>(GetController()))
	{
		AC->NotifyDamagedBy(DamageInstigator, AppliedDamage);
	}
}

void AMonsterCharacter::HandleDamageHitReact(float Damage, AActor* InstigatorActor,
                                             const FGameplayEffectContextHandle& EffectContext)
{
	if (!HasAuthority() || !bEnableHitReact || Damage <= 0.0f)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	// 무적은 ExecCalc_Damage에서 이미 데미지를 0으로 만든다. 여기 검사는 방어적 중복이다.
	const bool bIsInvincible = IsValid(ASC) && ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Invincible);

	/*
	 * 슈퍼아머는 무적과 다르다. 데미지는 그대로 받고 경직 반응만 건너뛴다.
	 * 도중에 끊기면 안 되는 어빌리티가 ActivationOwnedTags로 스스로 부여한다.
	 */
	const bool bIgnoresHitReact = IsValid(ASC) && ASC->HasMatchingGameplayTag(GameplayTagSystem::State_AI_SuperArmor);

	if (!IsValid(ASC) || bIsInvincible || bIgnoresHitReact)
	{
		return;
	}

	if (bUseAccumulatedHitReact)
	{
		AccumulateHitReactDamage(Damage, InstigatorActor, EffectContext);
		return;
	}

	SendHitReactEvent(InstigatorActor, EffectContext, Damage);
}

void AMonsterCharacter::SetUseMonsterStatUI(bool bUse)
{
	if (!HasAuthority() || bUseMonsterStatUI == bUse)
	{
		return;
	}
	bUseMonsterStatUI = bUse;
	OnRep_UseMonsterStatUI();
	ForceNetUpdate();
}

void AMonsterCharacter::OnRep_UseMonsterStatUI()
{
	if (!bUseMonsterStatUI)
	{
		if (IsValid(MonsterHealthBarComponent))
		{
			MonsterHealthBarComponent->SetVisibility(false);
		}
	}
	else
	{
		InitializeMonsterHealthBar();
	}
}
