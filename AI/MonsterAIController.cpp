// 프로젝트 설정의 설명 페이지에 저작권 정보를 입력하세요.

#include "GameCore/AI/MonsterAIController.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "GameCore/Character/BaseCharacter.h"
#include "GameCore/Attribute/CharacterAttributeSet.h"
#include "GameCore/Movement/MonsterMovementComponent.h"
#include "GameCore/Combat/EnemyPatternMovement.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "Components/StateTreeAIComponent.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameCore/AI/MonsterCombatDirectorSubsystem.h"
#include "GameCore/AI/MonsterTargetSelector.h"

AMonsterAIController::AMonsterAIController()
{
	/*
	 * 센스 서브오브젝트는 항상 만들어둔다.
	 *
	 * 시야 감지는 모든 몬스터가 공용으로 사용한다.
	 */
	UAIPerceptionComponent* NewPerceptionComponent =
	    CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
	SetPerceptionComponent(*NewPerceptionComponent);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
}

void AMonsterAIController::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	Super::GetActorEyesViewPoint(OutLocation, OutRotation);
	OutRotation.Pitch = FRotator::NormalizeAxis(OutRotation.Pitch + SightPitchOffset);
}

ETeamAttitude::Type AMonsterAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
	/*
	 * 쿼리 생성 시점에 한 번 판정된다. ASC 태그는 BeginPlay 이후에 붙어 시점이 어긋날 수 있으므로
	 * 생성자에서 정해지는 역할 태그를 읽는다. 사망·은신 같은 실시간 상태는 IsValidCombatTarget 이 맡는다.
	 */
	const ABaseCharacter* OtherCharacter = Cast<ABaseCharacter>(&Other);
	const bool bIsPlayer = OtherCharacter != nullptr &&
	                       OtherCharacter->GetCharacterRoleTag().MatchesTag(GameplayTagSystem::Character_Player);

	return bIsPlayer ? ETeamAttitude::Hostile : ETeamAttitude::Friendly;
}

void AMonsterAIController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	/*
	 * 주기 재평가. 이동보다 먼저 돌려서, 이번 틱에 타깃이 풀리면
	 * UpdateMovement가 곧바로 추격 경로를 정리하게 한다.
	 */
	if (const UWorld* World = GetWorld())
	{
		if (World->GetTimeSeconds() >= NextTargetEvaluationTime)
		{
			EvaluateTarget();
		}
	}

	UpdateMovement(DeltaSeconds);

#if !UE_BUILD_SHIPPING
	if (!bShowAIDebugMessages || !GEngine)
	{
		return;
	}

	const FString PawnName = GetNameSafe(GetPawn());
	const FString TargetName = GetNameSafe(TargetActor);
	const FString EvaluatorTargetName = GetNameSafe(DebugEvaluatorTargetActor.Get());
	const bool bDebugValuesMatch =
	    bHasTarget == bDebugEvaluatorHasTarget && TargetActor == DebugEvaluatorTargetActor.Get();
	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double EvaluatorTickAge = DebugEvaluatorLastTickTime >= 0.0 ? CurrentTime - DebugEvaluatorLastTickTime : -1.0;
	const FString TargetDebugMessage = FString::Printf(
	    TEXT("[AI: %s] CTRL=(%s, %s) | EVAL=(%s, %s) | Match=%s | EvalTicks=%llu | EvalAge=%.2fs"), *PawnName,
	    bHasTarget ? TEXT("True") : TEXT("False"), *TargetName, bDebugEvaluatorHasTarget ? TEXT("True") : TEXT("False"),
	    *EvaluatorTargetName, bDebugValuesMatch ? TEXT("YES") : TEXT("NO"), DebugEvaluatorTickCount, EvaluatorTickAge);

	const uint64 MessageKeyBase = static_cast<uint64>(GetUniqueID()) << 1;
	GEngine->AddOnScreenDebugMessage(MessageKeyBase, 0.0f, bDebugValuesMatch ? FColor::Green : FColor::Yellow,
	                                 TargetDebugMessage, false, FVector2D(0.9f));

	FString StateTreeDebugMessage = TEXT("StateTreeAIComponent is invalid.");
	if (IsValid(StateTreeAIComponent))
	{
		// 활성 State와 해당 State의 Task 목록을 기록해 전환 경로를 확인한다.
		StateTreeDebugMessage = StateTreeAIComponent->GetDebugInfoString();
	}

	if (StateTreeDebugMessage != LastStateTreeDebugMessage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[StateTreeCheck] State changed | Controller=%s | Pawn=%s | %s"),
		       *GetNameSafe(this), *GetNameSafe(GetPawn()), *StateTreeDebugMessage);
		LastStateTreeDebugMessage = StateTreeDebugMessage;
	}

	GEngine->AddOnScreenDebugMessage(MessageKeyBase + 1, 0.0f, FColor::Cyan, StateTreeDebugMessage, false,
	                                 FVector2D(0.75f));
#endif
}

void AMonsterAIController::UpdatePerceptionEvaluatorDebug(AActor* InTargetActor, const bool bInHasTarget)
{
	DebugEvaluatorTargetActor = InTargetActor;
	bDebugEvaluatorHasTarget = bInHasTarget;
	++DebugEvaluatorTickCount;
	DebugEvaluatorLastTickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
}

void AMonsterAIController::RequestEncounter(AActor* EncounterTarget)
{
	if (!IsValidCombatTarget(EncounterTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MonsterTarget] RequestEncounterRejected | Controller=%s Pawn=%s Candidate=%s"),
		       *GetNameSafe(this), *GetNameSafe(GetPawn()), *GetNameSafe(EncounterTarget));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[MonsterTarget] RequestEncounter | Controller=%s Pawn=%s Previous=%s New=%s"),
	       *GetNameSafe(this), *GetNameSafe(GetPawn()), *GetNameSafe(TargetActor), *GetNameSafe(EncounterTarget));

	/*
	 * 인스티게이터는 폴백이다. 섹터를 밟은 한 명에게 전원이 몰리지 않도록
	 * 곧바로 각자 재평가해서 자기 기준의 후보를 고르게 한다.
	 */
	EncounterFallbackTarget = EncounterTarget;
	EncounterFallbackUntil = GetWorld()->GetTimeSeconds() + EncounterTargetGraceTime;

	SetCurrentTarget(EncounterTarget, TEXT("Encounter"));
	EvaluateTarget();

	if (const ACharacter* MonsterCharacter = Cast<ACharacter>(GetPawn()))
	{
		if (UMonsterMovementComponent* MovementComponent =
		        Cast<UMonsterMovementComponent>(MonsterCharacter->GetCharacterMovement()))
		{
			// 속도만 0으로 만들면 TickComponent에 남아 있는 패턴 이동이 다음 틱에 다시 입력된다.
			// 지상형의 예약 재배치까지 함께 취소한 뒤 실제 속도/경로를 정리한다.
			MovementComponent->CancelPatternPreparation();
			MovementComponent->ResetMovementState();
		}
	}

	if (IsValid(StateTreeAIComponent))
	{
		StateTreeAIComponent->SendStateTreeEvent(GameplayTagSystem::Event_AI_EncounterRequested, FConstStructView(),
		                                         TEXT("EncounterTrigger"));
	}
}

void AMonsterAIController::OverrideSightRadius(const float NewSightRadius)
{
	if (NewSightRadius <= 0.0f || !IsValid(SightConfig) || !IsValid(PerceptionComponent))
	{
		return;
	}

	// 경계 깜빡임 방지용 LoseSight 여유는 원래 비율을 유지한다.
	const float LoseRatio = SightRadius > 0.0f ? LoseSightRadius / SightRadius : 1.2f;
	SightRadius = NewSightRadius;
	LoseSightRadius = NewSightRadius * LoseRatio;

	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	PerceptionComponent->ConfigureSense(*SightConfig);
	PerceptionComponent->RequestStimuliListenerUpdate();
}

AActor* AMonsterAIController::FindNearestActorWithTag(const FGameplayTag RequiredTag, const float Radius) const
{
	const APawn* MyPawn = GetPawn();

	if (!IsValid(MyPawn) || !RequiredTag.IsValid())
	{
		return nullptr;
	}

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(const_cast<APawn*>(MyPawn));

	TArray<AActor*> OverlappedActors;
	UKismetSystemLibrary::SphereOverlapActors(this, MyPawn->GetActorLocation(), Radius, ObjectTypes, nullptr,
	                                          IgnoreActors, OverlappedActors);

	AActor* NearestActor = nullptr;
	float NearestDistSq = MAX_FLT;

	for (AActor* Candidate : OverlappedActors)
	{
		const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Candidate);

		const UAbilitySystemComponent* CandidateASC =
		    AbilitySystemInterface != nullptr ? AbilitySystemInterface->GetAbilitySystemComponent() : nullptr;

		if (!IsValid(CandidateASC) || !CandidateASC->HasMatchingGameplayTag(RequiredTag) ||
		    CandidateASC->HasMatchingGameplayTag(GameplayTagSystem::State_Dead) ||
		    CandidateASC->HasMatchingGameplayTag(GameplayTagSystem::State_AI_Dormant))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyPawn->GetActorLocation(), Candidate->GetActorLocation());

		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestActor = Candidate;
		}
	}

	return NearestActor;
}

AActor* AMonsterAIController::FindNearestPossessionTarget(const FGameplayTagContainer& RequiredTags,
                                                          const FGameplayTagContainer& BlockedTags,
                                                          AActor* ExpectedTarget, const float Radius) const
{
	const APawn* MyPawn = GetPawn();
	if (!IsValid(MyPawn) || RequiredTags.IsEmpty())
	{
		return nullptr;
	}

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(const_cast<APawn*>(MyPawn));

	TArray<AActor*> Candidates;
	UKismetSystemLibrary::SphereOverlapActors(this, MyPawn->GetActorLocation(), Radius, ObjectTypes, nullptr,
	                                          IgnoreActors, Candidates);

	AActor* NearestTarget = nullptr;
	float NearestDistanceSq = MAX_FLT;

	for (AActor* Candidate : Candidates)
	{
		const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Candidate);
		const UAbilitySystemComponent* CandidateASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;

		if (!IsValid(CandidateASC) || !CandidateASC->HasAllMatchingGameplayTags(RequiredTags) ||
		    CandidateASC->HasAnyMatchingGameplayTags(BlockedTags))
		{
			continue;
		}

		const APawn* CandidatePawn = Cast<APawn>(Candidate);
		const AMonsterAIController* CandidateController =
		    CandidatePawn ? Cast<AMonsterAIController>(CandidatePawn->GetController()) : nullptr;

		if (!IsValid(CandidateController) || !CandidateController->HasPerceivedTarget())
		{
			continue;
		}

		if (IsValid(ExpectedTarget) && CandidateController->GetPerceivedTarget() != ExpectedTarget)
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared(MyPawn->GetActorLocation(), Candidate->GetActorLocation());

		if (DistanceSq < NearestDistanceSq)
		{
			NearestDistanceSq = DistanceSq;
			NearestTarget = Candidate;
		}
	}

	return NearestTarget;
}

void AMonsterAIController::StopAIForDeath()
{
	SuspendAI(TEXT("Monster died"));
}

void AMonsterAIController::ResumeAI()
{
	// 자다가 반납된 경우에도 일시정지가 남지 않게 먼저 깨운다
	SetSleeping(false);

	// 휴면 몹과 같은 출발점. 등장 방식이 잠금을 풀어 준다
	bMovementLocked = true;
	bWasActionCommitted = false;
	bHadTargetLastTick = false;
	bIsInterruptible = true;

	TargetActor = nullptr;
	bHasTarget = false;
	CurrentTargetAcquiredTime = -1.0;
	LastSeenTargets.Empty();
	ExhaustedSearchTargets.Empty();
	LastSeenArrivalTime = -1.0;
	LastDamagedTimeByInstigator.Empty();
	DamageThreatByInstigator.Empty();
	EncounterFallbackTarget = nullptr;
	EncounterFallbackUntil = -1.0;
	SelectedAction = FGameplayTag();

	if (PerceptionComponent)
	{
		PerceptionComponent->Activate(true);
		PerceptionComponent->RequestStimuliListenerUpdate();
	}

	if (StateTreeAIComponent)
	{
		StateTreeAIComponent->RestartLogic();
	}
}

void AMonsterAIController::SetSleeping(const bool bSleep)
{
	if (bSleeping == bSleep)
	{
		return;
	}

	bSleeping = bSleep;

	if (StateTreeAIComponent)
	{
		if (bSleep)
		{
			StateTreeAIComponent->PauseLogic(TEXT("DistanceSleep"));
		}
		else
		{
			StateTreeAIComponent->ResumeLogic(TEXT("DistanceWake"));
		}
	}

	if (PerceptionComponent)
	{
		if (bSleep)
		{
			PerceptionComponent->Deactivate();
		}
		else
		{
			PerceptionComponent->Activate(true);
			PerceptionComponent->RequestStimuliListenerUpdate();
		}
	}

	if (bSleep)
	{
		StopMovement();
	}

	SetActorTickEnabled(!bSleep);
}

void AMonsterAIController::SuspendAI(const TCHAR* Reason)
{
	// Tick에서 실행되는 UpdateMovement 차단
	bMovementLocked = true;

	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);

	if (const ACharacter* MonsterCharacter = Cast<ACharacter>(GetPawn()))
	{
		if (UMonsterMovementComponent* MovementComponent =
		        Cast<UMonsterMovementComponent>(MonsterCharacter->GetCharacterMovement()))
		{
			MovementComponent->ResetMovementState();
		}
	}

	TargetActor = nullptr;
	bHasTarget = false;
	CurrentTargetAcquiredTime = -1.0;
	LastSeenTargets.Empty();
	ExhaustedSearchTargets.Empty();
	LastSeenArrivalTime = -1.0;
	LastDamagedTimeByInstigator.Empty();
	EncounterFallbackTarget = nullptr;
	EncounterFallbackUntil = -1.0;

	// 죽은 몬스터가 배정과 토큰을 물고 있으면 남은 몬스터의 판단이 틀어진다.
	if (const UWorld* World = GetWorld())
	{
		if (UMonsterCombatDirectorSubsystem* Director = World->GetSubsystem<UMonsterCombatDirectorSubsystem>())
		{
			Director->UnregisterMonster(GetPawn());
		}
	}

	SelectedAction = FGameplayTag();

	if (PerceptionComponent)
	{
		PerceptionComponent->ForgetAll();
		PerceptionComponent->Deactivate();
	}

	// 시작 전이거나 이미 멈춘 트리에 StopLogic 을 부르면 컨텍스트 요구 조건 실패 경고만 남는다
	if (StateTreeAIComponent && StateTreeAIComponent->IsRunning())
	{
		StateTreeAIComponent->StopLogic(Reason);
	}
}

void AMonsterAIController::SendAIStateTreeEvent(FGameplayTag EventTag)
{
	if (!IsValid(StateTreeAIComponent) || !EventTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[StateTree] Event send failed: Controller=%s, Component=%s, Event=%s"),
		       *GetNameSafe(this), *GetNameSafe(StateTreeAIComponent), *EventTag.ToString());
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[StateTree] Event send: Controller=%s, Status=%s, Event=%s"), *GetNameSafe(this),
	       *UEnum::GetDisplayValueAsText(StateTreeAIComponent->GetStateTreeRunStatus()).ToString(),
	       *EventTag.ToString());

	StateTreeAIComponent->SendStateTreeEvent(EventTag, FConstStructView(), EventTag.GetTagName());
}

void AMonsterAIController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(PerceptionComponent))
	{
		UE_LOG(LogTemp, Error, TEXT("[MonsterAIController] %s: PerceptionComponent가 없습니다."), *GetPathName());
		return;
	}

	/*
	 * 네이티브 서브오브젝트를 추가하거나 상속 구조를 변경한 직후에는
	 * Live Coding으로 남아 있는 기존 BP CDO가 센스 설정 포인터를 null로
	 * 가지고 있을 수 있다. 크래시시키는 대신 런타임 설정을 복구한다.
	 */
	if (!IsValid(SightConfig))
	{
		SightConfig = NewObject<UAISenseConfig_Sight>(this);

		UE_LOG(
		    LogTemp, Warning,
		    TEXT(
		        "[MonsterAIController] %s: 유실된 SightConfig를 복구했습니다. 에디터 재시작 후 해당 AIController BP를 다시 컴파일하세요."),
		    *GetPathName());
	}

	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngle;
	// MaxAge 0 은 "영원히 기억"이다. 기억을 끄는 0 과 구분해 최소값을 둔다
	SightConfig->SetMaxAge(FMath::Max(SightMemoryDuration, 0.1f));
	// 피아 판정은 GetTeamAttitudeTowards 가 한다. 셋을 모두 켜면 엔진이 필터를 통째로 건너뛴다
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	PerceptionComponent->ConfigureSense(*SightConfig);

	PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());

	/*
	 * 몬스터마다 평가 시점을 어긋나게 둔다. 전원이 같은 프레임에 후보를
	 * 훑으면 스파이크가 생긴다. 난수가 아니라 UniqueID 기반이라 결정적이다.
	 */
	if (const UWorld* World = GetWorld())
	{
		const float Interval = FMath::Max(TargetEvaluationInterval, 0.05f);
		const float Offset = Interval * ((GetUniqueID() % 100) / 100.0f);
		NextTargetEvaluationTime = World->GetTimeSeconds() + Offset;
	}

	PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &AMonsterAIController::OnPerceptionUpdated);

	SetMovementLocked(true);
}

bool AMonsterAIController::NotifyIncomingAttack(AActor* Attacker, const FVector& AttackOrigin)
{
	APawn* TargetPawn = GetPawn();
	if (!IsValid(Attacker) || !IsValid(TargetPawn))
	{
		return false;
	}

	ComputeGunfireDirection(AttackOrigin);

	if (!bIsInterruptible)
	{
		UE_LOG(LogTemp, Warning, TEXT("[IncomingAttack] EvadeBlocked | Target=%s | Reason=NotInterruptible"),
		       *GetNameSafe(TargetPawn));
		return false;
	}

	if (!HasPerceivedTarget())
	{
		UE_LOG(LogTemp, Warning, TEXT("[IncomingAttack] EvadeBlocked | Target=%s | Reason=NoTarget"),
		       *GetNameSafe(TargetPawn));
		return false;
	}

	const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(TargetPawn);
	UAbilitySystemComponent* AbilitySystemComponent =
	    AbilitySystemInterface ? AbilitySystemInterface->GetAbilitySystemComponent() : nullptr;

	if (!IsValid(AbilitySystemComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("[IncomingAttack] EvadeBlocked | Target=%s | Reason=InvalidASC"),
		       *GetNameSafe(TargetPawn));
		return false;
	}

	FGameplayTagContainer EvadeTags;
	EvadeTags.AddTag(GameplayTagSystem::Ability_Enemy_Common_Evade);

	TArray<FGameplayAbilitySpec*> EvadeSpecs;
	AbilitySystemComponent->GetActivatableGameplayAbilitySpecsByAllMatchingTags(EvadeTags, EvadeSpecs);

	if (EvadeSpecs.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[IncomingAttack] EvadeBlocked | Target=%s | Reason=EvadeAbilityMissing"),
		       *GetNameSafe(TargetPawn));
		return false;
	}

	if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::Cooldown_AI_Evade))
	{
		UE_LOG(LogTemp, Warning, TEXT("[IncomingAttack] EvadeBlocked | Target=%s | Reason=Cooldown"),
		       *GetNameSafe(TargetPawn));
		return false;
	}

	const float ClampedEvadeChance = FMath::Clamp(EvadeChance, 0.0f, 1.0f);
	if (ClampedEvadeChance <= 0.0f || (ClampedEvadeChance < 1.0f && FMath::FRand() > ClampedEvadeChance))
	{
		UE_LOG(LogTemp, Warning, TEXT("[IncomingAttack] EvadeBlocked | Target=%s | Reason=Chance | Chance=%.2f"),
		       *GetNameSafe(TargetPawn), ClampedEvadeChance);
		return false;
	}

	// Evade GA를 직접 실행하지 않고 StateTree의 이벤트 전환으로 위임한다.
	// 실제 GA 실행과 상태 종료는 Evade 상태의 Activate Ability By Tag가 담당한다.
	SendAIStateTreeEvent(GameplayTagSystem::Event_AI_Evade_Requested);

	UE_LOG(LogTemp, Warning, TEXT("[IncomingAttack] EvadeEventSent | Target=%s | Attacker=%s | Origin=(%s)"),
	       *GetNameSafe(TargetPawn), *GetNameSafe(Attacker), *AttackOrigin.ToCompactString());

	return true;
}

void AMonsterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	const bool bIsSightStimulus = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();
	const TCHAR* SenseName = bIsSightStimulus ? TEXT("Sight") : TEXT("Other");

	UE_LOG(LogTemp, Warning, TEXT("[PerceptionRaw] Controller=%s Actor=%s Sense=%s Sensed=%s Tag=%s Location=(%s)"),
	       *GetName(), *GetNameSafe(Actor), SenseName, Stimulus.WasSuccessfullySensed() ? TEXT("True") : TEXT("False"),
	       *Stimulus.Tag.ToString(), *Stimulus.StimulusLocation.ToCompactString());

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
	{
		/*
		 * 감지 상태 변화는 즉시 재평가 트리거 중 하나일 뿐이다. 선정 규칙은
		 * EvaluateTarget 한 곳에만 두고, 주기 틱과 다른 트리거가 같은 함수를 쓴다.
		 */
		EvaluateTarget();

		/*
		 * UE 5.8 StateTree는 예약 Tick 중 잠들 수 있다.
		 * Perception 결과가 바뀌었을 때 이벤트를 보내 다음 Tick을 예약해야
		 * Evaluator가 TargetActor/bHasTarget을 복사하고 전환 조건을 다시 평가한다.
		 * 시작 전인 트리는 시작할 때 첫 평가를 하므로 이벤트가 필요 없다.
		 */
		if (IsValid(StateTreeAIComponent) && StateTreeAIComponent->IsRunning())
		{
			StateTreeAIComponent->SendStateTreeEvent(GameplayTagSystem::Event_AI_PerceptionChanged, FConstStructView(),
			                                         TEXT("MonsterAIController"));
		}
	}
}

bool AMonsterAIController::IsTargetVisible(const AActor* Target) const
{
	if (!IsValid(Target) || !IsValid(PerceptionComponent))
	{
		return false;
	}
	TArray<AActor*> VisibleActors;
	PerceptionComponent->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), VisibleActors);
	return VisibleActors.Contains(Target) && LineOfSightTo(Target);
}

bool AMonsterAIController::GetPursuitTargetLocation(const AActor* Target, FVector& OutLocation) const
{
	if (!IsValid(Target))
	{
		return false;
	}
	if (bTrackAllPlayers || IsTargetVisible(Target))
	{
		OutLocation = Target->GetActorLocation();
		return true;
	}
	const FLastSeenTarget* Memory = LastSeenTargets.Find(const_cast<AActor*>(Target));
	if (!Memory)
	{
		return false;
	}
	OutLocation = Memory->Location;
	return true;
}

bool AMonsterAIController::CanAttackTarget(const AActor* Target) const
{
	const APawn* SelfPawn = GetPawn();
	const UWorld* World = GetWorld();
	if (!SelfPawn || !World || !IsValid(Target) || (!bTrackAllPlayers && !IsTargetVisible(Target)))
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MonsterAttackVisibility), false, SelfPawn);
	Params.AddIgnoredActor(Target);
	FCollisionResponseParams Responses;
	Responses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, SelfPawn->GetActorLocation(), Target->GetActorLocation(),
	                                        ECC_Visibility, Params, Responses);
}

void AMonsterAIController::EvaluateTarget()
{
	const UWorld* World = GetWorld();
	const APawn* MyPawn = GetPawn();

	if (!World || !IsValid(MyPawn))
	{
		return;
	}

	NextTargetEvaluationTime = World->GetTimeSeconds() + FMath::Max(TargetEvaluationInterval, 0.05f);

	// 보이는 동안만 위치를 갱신한다. 시야 상실 이후에는 이 좌표를 고정한다.
	TArray<AActor*> VisibleActors;
	if (IsValid(PerceptionComponent))
	{
		PerceptionComponent->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), VisibleActors);
	}
	for (AActor* Visible : VisibleActors)
	{
		if (IsValidCombatTarget(Visible) && LineOfSightTo(Visible))
		{
			LastSeenTargets.FindOrAdd(Visible) = {Visible->GetActorLocation(), World->GetTimeSeconds()};
			ExhaustedSearchTargets.Remove(Visible);
		}
	}
	for (auto It = LastSeenTargets.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	TArray<AActor*> Candidates;
	GatherTargetCandidates(Candidates);

	if (Candidates.IsEmpty())
	{
		if (IsValid(TargetActor))
		{
			SetCurrentTarget(nullptr, TEXT("NoCandidate"));
		}
		return;
	}

	// 현재 타깃이 후보에서 빠졌다면(사망/은신/시야 이탈) 유지할 근거가 없다.
	const bool bCurrentValid = IsValid(TargetActor) && Candidates.Contains(TargetActor);

	/*
	 * 유지 시간 상한은 어느 셀렉터를 끼우든 지켜져야 하는 안전장치라
	 * 정책이 아니라 컨트롤러에 둔다. 상한에 걸리면 현재 타깃을 후보에서
	 * 아예 빼버리므로, 후보가 둘 이상이면 고정이 성립할 수 없다.
	 */
	const bool bForceSwitch =
	    bCurrentValid && MaxTargetHoldTime > 0.0f && GetTargetHoldTime() >= MaxTargetHoldTime && Candidates.Num() >= 2;

	if (bForceSwitch)
	{
		Candidates.Remove(TargetActor);
	}

	FTargetSelectionContext Context;
	Context.Controller = this;
	Context.SelfLocation = MyPawn->GetActorLocation();
	Context.HoldTime = GetTargetHoldTime();
	Context.ScoringRangeOverride = bTrackAllPlayers ? TrackScoringRange : 0.0f;

	// 강제 전환 중에는 현재 타깃을 비워, 셀렉터가 전환 문턱을 적용하지 않게 한다.
	Context.CurrentTarget = (bCurrentValid && !bForceSwitch) ? TargetActor.Get() : nullptr;

	Context.Candidates.Reserve(Candidates.Num());
	for (AActor* Candidate : Candidates)
	{
		Context.Candidates.Add(Candidate);
	}

	UMonsterTargetSelector* Selector = GetTargetSelector();
	if (!IsValid(Selector))
	{
		return;
	}

	AActor* Chosen = Selector->SelectTarget(Context);

	if (Chosen == TargetActor)
	{
		return;
	}

	const TCHAR* Reason = bForceSwitch ? TEXT("MaxHoldTime") : (bCurrentValid ? TEXT("Score") : TEXT("NoValidTarget"));

	SetCurrentTarget(Chosen, Reason);
}

UMonsterTargetSelector* AMonsterAIController::GetTargetSelector()
{
	/*
	 * BP에서 지정하지 않았으면 기본 정책을 만들어 쓴다. 셀렉터를 비워둔 몹이
	 * 조용히 타깃을 못 잡는 것보다, 기본값으로라도 동작하는 편이 낫다.
	 */
	if (!IsValid(TargetSelector))
	{
		TargetSelector = NewObject<UWeightedTargetSelector>(this);
	}

	return TargetSelector;
}

void AMonsterAIController::GatherTargetCandidates(TArray<AActor*>& OutCandidates) const
{
	OutCandidates.Reset();

	if (IsValid(PerceptionComponent))
	{
		TArray<AActor*> PerceivedActors;
		PerceptionComponent->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);

		for (AActor* Candidate : PerceivedActors)
		{
			if (IsValidCombatTarget(Candidate))
			{
				OutCandidates.Add(Candidate);
			}
		}
	}

	/*
	 * 시야 예외: 최근에 나를 때린 상대는 보이지 않아도 후보로 넣는다.
	 *
	 * 시야각이 정면 기준 편각이라 90이면 좌우 180도만 보고 뒤쪽 180도는 사각이다.
	 * 거기서 쏘는 플레이어는 영원히 어그로를 못 끈다. 시야각을 열어 두더라도
	 * 시야 밖에서 맞은 경우를 이 예외가 후보로 올려 준다.
	 */
	const UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();

	/*
	 * 정리와 예외 판정을 분리한다.
	 *
	 * 제거는 DamageMemoryRetention 기준으로 항상 수행한다. 예외를 꺼둔
	 * 경우(DamagedTargetMemoryDuration = 0)에도 이 루프를 타야 죽은 액터의
	 * 기록이 남지 않는다.
	 */
	for (auto It = LastDamagedTimeByInstigator.CreateIterator(); It; ++It)
	{
		AActor* Attacker = It.Key().Get();

		if (!IsValid(Attacker) || Now - It.Value() > DamageMemoryRetention)
		{
			It.RemoveCurrent();
			continue;
		}

		if (DamagedTargetMemoryDuration <= 0.0f || Now - It.Value() > DamagedTargetMemoryDuration)
		{
			continue;
		}

		if (IsValidCombatTarget(Attacker) && !OutCandidates.Contains(Attacker))
		{
			OutCandidates.Add(Attacker);
		}
	}

	// Threat는 약해져 0에 가까워지면 정리한다. 시야 후보가 아니어도 누적 상태는 유지돼야 하므로
	// 최근 피격 후보 정리와 분리한다.
	for (auto It = DamageThreatByInstigator.CreateIterator(); It; ++It)
	{
		AActor* Attacker = It.Key().Get();
		if (!IsValid(Attacker) || GetDamageThreatNormalized(Attacker) <= KINDA_SMALL_NUMBER)
		{
			It.RemoveCurrent();
		}
	}

	// 전원 추적: 못 봐서 타깃이 없는 상태를 없앤다. 조우 직후 시야가 갱신되기 전에도 후보가 있다
	if (bTrackAllPlayers)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PlayerController = It->Get();
			APawn* CandidatePawn = IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;
			if (IsValidCombatTarget(CandidatePawn))
			{
				OutCandidates.AddUnique(CandidatePawn);
			}
		}
	}

	/*
	 * 시선 기억: 보이는 후보(시야 + 피격 예외 + 전원 추적)가 하나도 없을 때만 쓴다.
	 * 벽 뒤로 돌아간 상대를 잠깐 더 쫓게 하되, 눈앞의 다른 플레이어보다 앞서지는 못한다.
	 * 같은 필터(IsValidCombatTarget)를 거치므로 죽었거나 은신한 상대는 기억에서도 빠진다
	 */
	if (OutCandidates.IsEmpty() && SightMemoryDuration > 0.0f && IsValid(PerceptionComponent))
	{
		TArray<AActor*> KnownActors;
		PerceptionComponent->GetKnownPerceivedActors(UAISense_Sight::StaticClass(), KnownActors);

		for (AActor* Candidate : KnownActors)
		{
			if (IsValidCombatTarget(Candidate))
			{
				OutCandidates.Add(Candidate);
			}
		}
	}

	// 조우 폴백: 위 어떤 경로로도 후보가 없을 때만. 시야가 잡히면 그쪽이 우선한다
	if (OutCandidates.IsEmpty() && Now <= EncounterFallbackUntil)
	{
		AActor* Fallback = EncounterFallbackTarget.Get();
		if (IsValidCombatTarget(Fallback))
		{
			OutCandidates.Add(Fallback);
		}
	}
	if (!bTrackAllPlayers)
	{
		if (IsValidCombatTarget(TargetActor) && LastSeenTargets.Contains(TargetActor) &&
		    !ExhaustedSearchTargets.Contains(TargetActor))
		{
			OutCandidates.AddUnique(TargetActor);
		}
		OutCandidates.RemoveAll(
		    [&](AActor* Candidate)
		    {
			    return ExhaustedSearchTargets.Contains(Candidate) ||
			           (!LastSeenTargets.Contains(Candidate) && !IsTargetVisible(Candidate));
		    });
	}
}

void AMonsterAIController::SetTrackAllPlayers(const bool bEnable, const float ScoringRange)
{
	bTrackAllPlayers = bEnable;
	TrackScoringRange = bEnable ? FMath::Max(ScoringRange, 0.0f) : 0.0f;
}

void AMonsterAIController::SetCurrentTarget(AActor* NewTarget, const TCHAR* Reason)
{
	if (TargetActor == NewTarget)
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
	       TEXT("[MonsterTarget] Switch | Controller=%s Pawn=%s Prev=%s New=%s Reason=%s HoldTime=%.1f"),
	       *GetNameSafe(this), *GetNameSafe(GetPawn()), *GetNameSafe(TargetActor), *GetNameSafe(NewTarget), Reason,
	       GetTargetHoldTime());

	LastSeenArrivalTime = -1.0;
	TargetActor = NewTarget;
	bHasTarget = IsValid(NewTarget);

	const UWorld* World = GetWorld();
	CurrentTargetAcquiredTime = (bHasTarget && World) ? World->GetTimeSeconds() : -1.0;
	// 마지막 목격 위치 탐색 종료도 시야 이벤트 없이 StateTree에 전달한다.
	if (IsValid(StateTreeAIComponent) && StateTreeAIComponent->IsRunning())
	{
		StateTreeAIComponent->SendStateTreeEvent(GameplayTagSystem::Event_AI_PerceptionChanged, FConstStructView(),
		                                         TEXT("MonsterAIController"));
	}

	/*
	 * 배정 현황을 디렉터에 알린다. 다른 몬스터의 혼잡도 계산과 어택 토큰
	 * 상한이 이 값을 근거로 돌아간다.
	 */
	if (World)
	{
		if (UMonsterCombatDirectorSubsystem* Director = World->GetSubsystem<UMonsterCombatDirectorSubsystem>())
		{
			Director->ReportTarget(GetPawn(), TargetActor);
		}
	}
}

float AMonsterAIController::GetTargetHoldTime() const
{
	const UWorld* World = GetWorld();

	if (!World || CurrentTargetAcquiredTime < 0.0)
	{
		return 0.0f;
	}

	return static_cast<float>(World->GetTimeSeconds() - CurrentTargetAcquiredTime);
}

void AMonsterAIController::NotifyDamagedBy(AActor* DamageInstigator, const float AppliedDamage)
{
	if (!IsValidCombatTarget(DamageInstigator))
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		const double Now = World->GetTimeSeconds();
		LastDamagedTimeByInstigator.Add(DamageInstigator, Now);

		const IAbilitySystemInterface* OwnerASI = Cast<IAbilitySystemInterface>(GetPawn());
		const UAbilitySystemComponent* OwnerASC = OwnerASI ? OwnerASI->GetAbilitySystemComponent() : nullptr;
		const float MaxHealth =
		    IsValid(OwnerASC) ? OwnerASC->GetNumericAttribute(UCharacterAttributeSet::GetMaxHealthAttribute()) : 0.0f;

		if (AppliedDamage > 0.0f && MaxHealth > KINDA_SMALL_NUMBER)
		{
			FMonsterDamageThreatEntry& ThreatEntry = DamageThreatByInstigator.FindOrAdd(DamageInstigator);
			const float ThreatCap = FMath::Max(DamageThreatCap, KINDA_SMALL_NUMBER);
			const float DecayedThreat = GetDamageThreatNormalized(DamageInstigator) * ThreatCap;
			ThreatEntry.Value = FMath::Min(DecayedThreat + AppliedDamage / MaxHealth, ThreatCap);
			ThreatEntry.LastUpdatedTime = Now;
		}
	}

	// 이미 그 상대를 보고 있으면 즉시 재평가할 이유가 없다. 가산점만 기록해 둔다.
	if (DamageInstigator == TargetActor)
	{
		return;
	}

	EvaluateTarget();
}

bool AMonsterAIController::WasRecentlyDamagedBy(const AActor* DamageInstigator, const float InWindow) const
{
	const UWorld* World = GetWorld();

	if (!World || !IsValid(DamageInstigator) || InWindow <= 0.0f)
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();

	/*
	 * 여기서는 읽기만 한다. 정리는 GatherTargetCandidates가 맡는다.
	 * 이 함수의 InWindow(셀렉터의 가산점 창)와 후보 예외의 기억 창은 서로
	 * 다른 값일 수 있어서, 여기서 지우면 짧은 쪽이 긴 쪽 기록을 없애버린다.
	 */
	const double* LastTime = LastDamagedTimeByInstigator.Find(const_cast<AActor*>(DamageInstigator));

	return LastTime != nullptr && (Now - *LastTime) <= InWindow;
}

float AMonsterAIController::GetDamageThreatNormalized(const AActor* DamageInstigator) const
{
	const UWorld* World = GetWorld();
	if (!World || !IsValid(DamageInstigator))
	{
		return 0.0f;
	}

	const FMonsterDamageThreatEntry* ThreatEntry = DamageThreatByInstigator.Find(const_cast<AActor*>(DamageInstigator));
	if (!ThreatEntry || ThreatEntry->Value <= 0.0f || ThreatEntry->LastUpdatedTime < 0.0)
	{
		return 0.0f;
	}

	const double Elapsed = FMath::Max(World->GetTimeSeconds() - ThreatEntry->LastUpdatedTime, 0.0);
	const float HalfLife = FMath::Max(DamageThreatHalfLife, KINDA_SMALL_NUMBER);
	const float DecayedThreat = ThreatEntry->Value * FMath::Pow(0.5f, static_cast<float>(Elapsed) / HalfLife);
	return FMath::Clamp(DecayedThreat / FMath::Max(DamageThreatCap, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
}

bool AMonsterAIController::IsValidCombatTarget(const AActor* Candidate) const
{
	if (!IsValid(Candidate) || Candidate == GetPawn())
	{
		return false;
	}

	const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Candidate);
	const UAbilitySystemComponent* CandidateASC =
	    AbilitySystemInterface != nullptr ? AbilitySystemInterface->GetAbilitySystemComponent() : nullptr;

	/*
	 * 은신 중인 플레이어는 후보에서 뺀다. GA_SniperSkill_Camo가 붙이는 태그다.
	 * 이걸 안 보면 잠복한 스나이퍼를 계속 타깃으로 물고 있게 된다.
	 */
	return IsValid(CandidateASC) && CandidateASC->HasMatchingGameplayTag(GameplayTagSystem::Character_Player) &&
	       !CandidateASC->HasMatchingGameplayTag(GameplayTagSystem::State_Dead) &&
	       !CandidateASC->HasMatchingGameplayTag(GameplayTagSystem::State_Skill_Camo_Stealth);
}

void AMonsterAIController::ComputeGunfireDirection(const FVector& StimulusLocation)
{
	const APawn* MyPawn = GetPawn();

	if (!IsValid(MyPawn))
	{
		return;
	}

	FVector ToSound = StimulusLocation - MyPawn->GetActorLocation();

	ToSound.Z = 0.0f;
	const FVector SoundDirection = ToSound.GetSafeNormal();
	const FVector Forward = MyPawn->GetActorForwardVector();
	const FVector Right = MyPawn->GetActorRightVector();
	const float ForwardDot = FVector::DotProduct(SoundDirection, Forward);
	const float RightDot = FVector::DotProduct(SoundDirection, Right);

	// 내적이 음수면 왼쪽, 양수면 오른쪽에서 난 소리다.
	GunfireDirectionSign = RightDot;

	const TCHAR* DirectionName = nullptr;
	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		DirectionName = ForwardDot >= 0.0f ? TEXT("Front") : TEXT("Back");
	}
	else
	{
		DirectionName = RightDot >= 0.0f ? TEXT("Right") : TEXT("Left");
	}

	UE_LOG(LogTemp, Warning,
	       TEXT("[GunfireDirection] Pawn=%s Stimulus=(%s) ForwardDot=%.3f RightDot=%.3f Direction=%s"),
	       *GetNameSafe(MyPawn), *StimulusLocation.ToCompactString(), ForwardDot, RightDot, DirectionName);
}

void AMonsterAIController::StopPursuitPath(UMonsterMovementComponent* MovementComponent)
{
	/*
	 * UpdateMovement는 매 틱 도므로 무조건 정리하면 StopMovement가 매 프레임 불린다.
	 * 그러면 어빌리티가 자체적으로 낸 이동 요청까지 다음 틱에 취소된다.
	 * 실제로 경로를 따라가는 중일 때만 끊는다.
	 */
	const UPathFollowingComponent* PathFollowing = GetPathFollowingComponent();
	if (!IsValid(MovementComponent) || PathFollowing == nullptr ||
	    PathFollowing->GetStatus() == EPathFollowingStatus::Idle)
	{
		return;
	}

	MovementComponent->ResetMovementState();
}

void AMonsterAIController::UpdateMovement(const float DeltaSeconds)
{
	ACharacter* MyCharacter = Cast<ACharacter>(GetPawn());
	UMonsterMovementComponent* MovementComponent =
	    IsValid(MyCharacter) ? Cast<UMonsterMovementComponent>(MyCharacter->GetCharacterMovement()) : nullptr;

	/*
	 * 커밋된 행동이 시작된 틱에 남은 이동을 한 번 끊는다.
	 *
	 * 행동 중엔 아래에서 새 추격 요청을 내지 않을 뿐, 이미 걸린 이동은 그대로 남는다.
	 * 지상은 MoveToActor 경로가 계속 따라가고, 비행은 관성으로 미끄러진다.
	 * 각 이동 컴포넌트의 ResetMovementState 가 경로 취소와 급정지를 맡는다.
	 * 패턴 이동(Move During Ability)은 GA가 직접 내는 이동이라 건드리지 않는다.
	 * 아래의 조기 반환보다 먼저 두어야 태그가 빠진 틱을 놓치지 않는다.
	 */
	const IAbilitySystemInterface* OwnerAbilitySystemInterface = Cast<IAbilitySystemInterface>(GetPawn());
	const UAbilitySystemComponent* OwnerAbilitySystemComponent =
	    OwnerAbilitySystemInterface ? OwnerAbilitySystemInterface->GetAbilitySystemComponent() : nullptr;
	const bool bActionCommitted =
	    IsValid(OwnerAbilitySystemComponent) &&
	    OwnerAbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_AI_ActionCommitted);

	if (bActionCommitted && !bWasActionCommitted && IsValid(MovementComponent) &&
	    !MovementComponent->IsPatternMovementActive())
	{
		MovementComponent->ResetMovementState();
	}
	bWasActionCommitted = bActionCommitted;

	if (IsTargetVisible(TargetActor))
	{
		LastSeenTargets.FindOrAdd(TargetActor) = {TargetActor->GetActorLocation(), GetWorld()->GetTimeSeconds()};
		ExhaustedSearchTargets.Remove(TargetActor);
	}
	const bool bHasTargetNow = IsValid(TargetActor);
	if (bMovementLocked || !bHasTargetNow)
	{
		if (!bMovementLocked && !bHasTargetNow)
		{
			/*
			 * 일반 몬스터는 타깃 상실 시 즉시 멈춘다. 매 틱이 아니라 타깃을 잃은 그 틱에만 멈춘다 —
			 * 매 틱 속도를 지우면 순찰처럼 타깃 없이 움직이는 이동이 가속하지 못하고 기어간다.
			 * 잠금 중에 타깃을 잃었으면 플래그가 그대로라 잠금이 풀리는 틱에 한 번 멈춘다.
			 * 앵커 공전·높이 보정처럼 타깃 자체가 필요 없는 패턴 이동은 여기서 속도를 지우면 안 된다.
			 * 타깃 추적 이동은 구현체가 false를 반환하므로 기존 정지 규칙을 따른다.
			 */
			if (bHadTargetLastTick && IsValid(MovementComponent))
			{
				const IEnemyPatternMovement* PatternMovement = Cast<IEnemyPatternMovement>(MovementComponent);
				if (PatternMovement == nullptr || !PatternMovement->CanContinuePatternMovementWithoutTarget())
				{
					MovementComponent->ResetMovementState();
				}
			}
			bHadTargetLastTick = false;
		}
		return;
	}

	bHadTargetLastTick = true;

	// 추격 이동을 아예 쓰지 않는 몬스터(순간이동 전용)는 여기서 끝낸다.
	if (!bEnablePursuitMovement)
	{
		StopPursuitPath(MovementComponent);
		return;
	}

	/*
	 * State.AI.ActionCommitted는 GA_MeleeAttack/GA_Dash 등이 활성화 중 자동으로
	 * 소유자에게 부여한다(ActivationOwnedTags). 커밋된 행동 실행 중엔 이동을 멈춘다.
	 * State.AI.MovementDisabled는 GE나 GA로 일시적 이동 봉인을 걸 때 쓴다.
	 */
	if (const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(GetPawn()))
	{
		if (const UAbilitySystemComponent* AbilitySystemComponent = AbilitySystemInterface->GetAbilitySystemComponent())
		{
			if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_AI_ActionCommitted))
			{
				return;
			}

			if (AbilitySystemComponent->HasMatchingGameplayTag(GameplayTagSystem::State_AI_MovementDisabled))
			{
				StopPursuitPath(MovementComponent);
				return;
			}
		}
	}

	if (!IsValid(MyCharacter))
	{
		return;
	}

	if (IsValid(MovementComponent) && !bTrackAllPlayers && !IsTargetVisible(TargetActor))
	{
		const FLastSeenTarget* Memory = LastSeenTargets.Find(TargetActor);
		const double Now = GetWorld()->GetTimeSeconds();
		const bool bArrived = Memory && FVector::DistSquared2D(MyCharacter->GetActorLocation(), Memory->Location) <=
		                                    FMath::Square(LastSeenAcceptanceRadius);
		if (bArrived && LastSeenArrivalTime < 0.0)
		{
			LastSeenArrivalTime = Now;
		}
		if (!Memory || Now - Memory->Time >= LastSeenPursuitTimeout ||
		    (LastSeenArrivalTime >= 0.0 && Now - LastSeenArrivalTime >= LastSeenSearchWait))
		{
			ExhaustedSearchTargets.Add(TargetActor);
			StopPursuitPath(MovementComponent);
			SetCurrentTarget(nullptr, TEXT("LastSeenSearchFinished"));
			return;
		}
		if (bArrived)
		{
			StopPursuitPath(MovementComponent);
		}
		else
		{
			MovementComponent->UpdateMovementTowardLocation(Memory->Location, DeltaSeconds);
		}
		return;
	}
	LastSeenArrivalTime = -1.0;
	if (IsValid(MovementComponent))
	{
		MovementComponent->UpdateMovementTowardTarget(TargetActor, DeltaSeconds);
	}
}

void AMonsterAIController::SetMovementLocked(const bool bLocked)
{
	bMovementLocked = bLocked;
}

void AMonsterAIController::RestoreDefaultTargetTracking()
{
	const auto* Defaults = GetClass()->GetDefaultObject<AMonsterAIController>();
	SetTrackAllPlayers(Defaults->bTrackAllPlayers, Defaults->TrackScoringRange);
}
