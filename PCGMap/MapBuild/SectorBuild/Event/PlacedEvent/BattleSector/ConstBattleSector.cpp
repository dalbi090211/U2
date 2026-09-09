#include "ConstBattleSector.h"

#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameCore/AI/MonsterAIController.h"

void AConstBattleSector::PostLoad()
{
	Super::PostLoad();

	if (!BattleTargets.IsEmpty() || TargetMonsters.IsEmpty())
	{
		return;
	}

	for (AMonsterCharacter* Monster : TargetMonsters)
	{
		if (!IsValid(Monster))
		{
			continue;
		}

		FBattleMonsterTarget& Target = BattleTargets.AddDefaulted_GetRef();
		Target.Monster = Monster;
		Target.SpawnType = Monster->GetEmergeSpawnType();
		Target.EmergeMontage = Monster->GetEmergeMontageOverride();
		Target.bUseTurnSection = Monster->ShouldUseEmergeTurnSection();
		Target.bUseTurnRotationWarp = Monster->ShouldUseEmergeTurnRotationWarp();
	}
}

void AConstBattleSector::BeginPlay()
{
	Super::BeginPlay();
	
	for (const FBattleMonsterTarget& Target : BattleTargets)
	{
		if (IsValid(Target.Monster))
		{
			Target.Monster->Hide();
		}
	}
}

void AConstBattleSector::Execute(const UObject* WorldContext)
{
	AActor* InstigatorActor = const_cast<AActor*>(Cast<AActor>(WorldContext));

	if (EncounterStartTiming == EBattleEncounterStartTiming::WithPreEvents)
	{
		//완료를 기다리지 않고 스폰과 동시에 재생
		RunEventGroups(PreEvents);
		StartEncounter(InstigatorActor);
		return;
	}

	StartPreEvents(InstigatorActor);
}

void AConstBattleSector::StartPreEvents(AActor* EncounterInstigator)
{
	PendingEncounterInstigator = EncounterInstigator;
	PendingPreEventCount = 0;
	NextPreEventId = 0;
	CompletedPreEventIds.Reset();

	for (const FTimedEventGroup& Group : PreEvents)
	{
		for (const TScriptInterface<ISectorEvent>& Event : Group.AllEvents())
		{
			ISectorEvent* Impl = Event.GetInterface();
			if (Impl && Event.GetObject() && Impl != static_cast<ISectorEvent*>(this))
			{
				++PendingPreEventCount;
			}
		}
	}

	if (PendingPreEventCount == 0)
	{
		StartEncounter(EncounterInstigator);
		return;
	}

	for (const FTimedEventGroup& Group : PreEvents)
	{
		for (const TScriptInterface<ISectorEvent>& Event : Group.AllEvents())
		{
			ISectorEvent* Impl = Event.GetInterface();
			if (!Impl || !Event.GetObject() || Impl == static_cast<ISectorEvent*>(this))
			{
				continue;
			}

			const int32 EventId = NextPreEventId++;
			if (Group.Delay <= 0.0f)
			{
				ExecutePreEvent(Event, EventId);
			}
			else
			{
				FTimerHandle TimerHandle;
				GetWorldTimerManager().SetTimer(
					TimerHandle,
					FTimerDelegate::CreateUObject(
						this,
						&AConstBattleSector::ExecutePreEvent,
						Event,
						EventId),
					Group.Delay,
					false);
			}
		}
	}
}

void AConstBattleSector::ExecutePreEvent(TScriptInterface<ISectorEvent> Event, int32 EventId)
{
	ISectorEvent* Impl = Event.GetInterface();
	UObject* EventObject = Event.GetObject();
	if (!Impl || !IsValid(EventObject))
	{
		OnPreEventCompleted(EventId);
		return;
	}

	if (IBattlePreEventCompletion* CompletionEvent = Cast<IBattlePreEventCompletion>(EventObject))
	{
		CompletionEvent->ExecuteBattlePreEvent(
			this,
			FSimpleDelegate::CreateUObject(
				this,
				&AConstBattleSector::OnPreEventCompleted,
				EventId));
		return;
	}

	Impl->Execute(this);
	OnPreEventCompleted(EventId);
}

void AConstBattleSector::OnPreEventCompleted(int32 EventId)
{
	if (CompletedPreEventIds.Contains(EventId))
	{
		return;
	}

	CompletedPreEventIds.Add(EventId);
	--PendingPreEventCount;
	if (PendingPreEventCount == 0)
	{
		StartEncounter(PendingEncounterInstigator.Get());
	}
}

void AConstBattleSector::StartEncounter(AActor* EncounterInstigator)
{
	//체인 실행 시 호출자 이벤트 액터가 넘어오므로 플레이어 폰으로 대체한다
	const APawn* InstigatorPawn = Cast<APawn>(EncounterInstigator);
	if (!InstigatorPawn || !InstigatorPawn->IsPlayerControlled())
	{
		EncounterInstigator = FindNearestPlayerPawn();
	}

	if (!IsValid(EncounterInstigator))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BattleSector] Encounter start rejected | Sector=%s | Reason=InstigatorInvalid"),
			*GetNameSafe(this));
		return;
	}

	ActiveMonsters.Reset();
	EmergeFinishedMonsters.Reset();
	bCombatReleased = false;
	const bool bWaitForAllEmerge =
		CombatActivationPolicy == EBattleCombatActivationPolicy::AfterAllTargetsEmerged;
	const bool bWaitForCombatRelease =
		CombatActivationPolicy != EBattleCombatActivationPolicy::PerMonsterAfterEmerge;

	for (const FBattleMonsterTarget& Target : BattleTargets)
	{
		AMonsterCharacter* Monster = Target.Monster.Get();
		if (!IsValid(Monster))
		{
			continue;
		}

		Monster->ConfigureBattleEmerge(
			Target.SpawnType,
			Target.EmergeMontage.Get(),
			Target.bUseTurnSection,
			Target.bUseTurnRotationWarp);
		Monster->SetWaitForBattleCombatRelease(bWaitForCombatRelease);

		if (EncounterSightRadius > 0.0f)
		{
			if (AMonsterAIController* AC = Cast<AMonsterAIController>(Monster->GetController()))
			{
				AC->OverrideSightRadius(EncounterSightRadius);
			}
		}

		Monster->OnMonsterDeath.AddUniqueDynamic(this, &AConstBattleSector::popMonster);
		if (bWaitForAllEmerge)
		{
			Monster->OnEmergeFinished.AddUniqueDynamic(
				this,
				&AConstBattleSector::OnTargetEmergeFinished);
		}

		if (IEncounterTargetInterface::Execute_RequestEncounter(
			Monster,
			EncounterInstigator))
		{
			ActiveMonsters.Add(Monster);
		}
		else
		{
			Monster->SetWaitForBattleCombatRelease(false);
			Monster->OnMonsterDeath.RemoveDynamic(this, &AConstBattleSector::popMonster);
			Monster->OnEmergeFinished.RemoveDynamic(this, &AConstBattleSector::OnTargetEmergeFinished);
		}
	}

	if (TryCheck())
	{
		RunEventGroups(PostEvents);
	}
}

APawn* AConstBattleSector::FindNearestPlayerPawn() const
{
	APawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	const FVector MyLocation = GetActorLocation();

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->IsValid() ? It->Get()->GetPawn() : nullptr;
		if (!IsValid(Pawn))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(MyLocation, Pawn->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Pawn;
		}
	}

	return Nearest;
}

void AConstBattleSector::OnTargetEmergeFinished(AMonsterCharacter* Monster)
{
	if (!IsValid(Monster) || !ActiveMonsters.Contains(Monster))
	{
		return;
	}

	EmergeFinishedMonsters.AddUnique(Monster);
	TryReleaseSynchronizedCombat();
}

void AConstBattleSector::TryReleaseSynchronizedCombat()
{
	if (bCombatReleased
		|| CombatActivationPolicy != EBattleCombatActivationPolicy::AfterAllTargetsEmerged
		|| ActiveMonsters.IsEmpty()
		|| EmergeFinishedMonsters.Num() != ActiveMonsters.Num())
	{
		return;
	}

	bCombatReleased = true;
	for (AMonsterCharacter* Monster : ActiveMonsters)
	{
		if (IsValid(Monster))
		{
			Monster->ReleaseBattleCombat();
		}
	}
}

void AConstBattleSector::popMonster(AMonsterCharacter* popMonster)
{
	ActiveMonsters.Remove(popMonster);
	EmergeFinishedMonsters.Remove(popMonster);
	popMonster->OnEmergeFinished.RemoveDynamic(this, &AConstBattleSector::OnTargetEmergeFinished);
	TryReleaseSynchronizedCombat();

	if (TryCheck())
	{
		RunEventGroups(PostEvents);
	}
}
