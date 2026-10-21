#include "ConstBattleSector.h"

#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameCore/AI/MonsterAIController.h"
#include "GameCore/Enemy/Spawn/SpawnGroupLinker.h"
#include "GameCore/Enemy/Spawn/SpawnPoint.h"

void AConstBattleSector::PostLoad()
{
	Super::PostLoad();

	// 변환 스크립트가 읽을 수 있도록 아주 옛 형식(TargetMonsters)만 BattleTargets 로 옮겨 둔다
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
				    FTimerDelegate::CreateUObject(this, &AConstBattleSector::ExecutePreEvent, Event, EventId),
				    Group.Delay, false);
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
		    this, FSimpleDelegate::CreateUObject(this, &AConstBattleSector::OnPreEventCompleted, EventId));
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

TArray<ASpawnPoint*> AConstBattleSector::GatherSpawnPoints() const
{
	TArray<ASpawnPoint*> Result;
	for (ASpawnPoint* Point : SpawnPoints)
	{
		if (IsValid(Point))
		{
			Result.AddUnique(Point);
		}
	}

	for (ASpawnPoint* Point : FSpawnGroupLinker::FindSpawnPoints(this, SpawnPointGroupId))
	{
		Result.AddUnique(Point);
	}

	return Result;
}

void AConstBattleSector::StartEncounter(AActor* EncounterInstigator)
{
	if (!HasAuthority())
	{
		return;
	}

	//체인 실행 시 호출자 이벤트 액터가 넘어오므로 플레이어 폰으로 대체한다
	const APawn* InstigatorPawn = Cast<APawn>(EncounterInstigator);
	if (!InstigatorPawn || !InstigatorPawn->IsPlayerControlled())
	{
		EncounterInstigator = FindNearestPlayerPawn();
	}

	if (!IsValid(EncounterInstigator))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BattleSector] Encounter start rejected | Sector=%s | Reason=InstigatorInvalid"),
		       *GetNameSafe(this));
		return;
	}

	ActiveMonsters.Reset();
	EmergeWaitingMonsters.Reset();
	EmergeFinishedMonsters.Reset();
	bCombatReleased = false;
	const bool bWaitForAllEmerge = CombatActivationPolicy == EBattleCombatActivationPolicy::AfterAllTargetsEmerged;
	const bool bWaitForCombatRelease = CombatActivationPolicy != EBattleCombatActivationPolicy::PerMonsterAfterEmerge;

	for (ASpawnPoint* Point : GatherSpawnPoints())
	{
		// 풀에서 꺼내 등장 방식까지 적용한다. 이미 꺼낸 포인트면 쥐고 있는 몹을 그대로 센다
		Point->Execute(EncounterInstigator);

		AMonsterCharacter* Monster = Point->GetSpawnedMonster();
		if (!IsValid(Monster) || !Point->HasAliveMonster())
		{
			continue;
		}

		/*
		 * 등장 GA 는 조우 이벤트를 받은 다음 틱 이후에 돈다. 같은 프레임 안에 설정하면
		 * GA 가 끝날 때 이 값을 보고 동시 전투 시작을 기다린다
		 */
		if (Point->UsesEmerge())
		{
			Monster->SetWaitForBattleCombatRelease(bWaitForCombatRelease);
			if (bWaitForAllEmerge)
			{
				EmergeWaitingMonsters.AddUnique(Monster);
				Monster->OnEmergeFinished.AddUniqueDynamic(this, &AConstBattleSector::OnTargetEmergeFinished);
			}
		}

		if (EncounterSightRadius > 0.0f)
		{
			if (AMonsterAIController* AC = Cast<AMonsterAIController>(Monster->GetController()))
			{
				AC->OverrideSightRadius(EncounterSightRadius);
			}
		}

		Monster->OnMonsterDeath.AddUniqueDynamic(this, &AConstBattleSector::popMonster);
		ActiveMonsters.AddUnique(Monster);
	}

	UE_LOG(LogTemp, Log, TEXT("[BattleSector] Encounter | Sector=%s | Monsters=%d"), *GetNameSafe(this),
	       ActiveMonsters.Num());

	// 나올 몹이 하나도 없으면 바로 끝난 전투다. 막히면 연결된 문이 영영 안 열린다
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
	if (!IsValid(Monster) || !EmergeWaitingMonsters.Contains(Monster))
	{
		return;
	}

	EmergeFinishedMonsters.AddUnique(Monster);
	TryReleaseSynchronizedCombat();
}

void AConstBattleSector::TryReleaseSynchronizedCombat()
{
	if (bCombatReleased || CombatActivationPolicy != EBattleCombatActivationPolicy::AfterAllTargetsEmerged ||
	    EmergeWaitingMonsters.IsEmpty() || EmergeFinishedMonsters.Num() != EmergeWaitingMonsters.Num())
	{
		return;
	}

	bCombatReleased = true;
	for (AMonsterCharacter* Monster : EmergeWaitingMonsters)
	{
		if (IsValid(Monster))
		{
			Monster->ReleaseBattleCombat();
		}
	}
}

void AConstBattleSector::popMonster(AMonsterCharacter* popMonster)
{
	if (!ActiveMonsters.Contains(popMonster))
	{
		return;
	}

	ActiveMonsters.Remove(popMonster);
	EmergeWaitingMonsters.Remove(popMonster);
	EmergeFinishedMonsters.Remove(popMonster);
	popMonster->OnMonsterDeath.RemoveDynamic(this, &AConstBattleSector::popMonster);
	popMonster->OnEmergeFinished.RemoveDynamic(this, &AConstBattleSector::OnTargetEmergeFinished);
	TryReleaseSynchronizedCombat();

	if (TryCheck())
	{
		RunEventGroups(PostEvents);
	}
}

void AConstBattleSector::ReplaceTrackedMonster(AMonsterCharacter* Parent, AMonsterCharacter* Left,
                                               AMonsterCharacter* Right)
{
	if (!HasAuthority() || !TracksMonster(Parent) || !IsValid(Left) || !IsValid(Right))
	{
		return;
	}
	for (AMonsterCharacter* Child : {Left, Right})
	{
		ActiveMonsters.AddUnique(Child);
		Child->OnMonsterDeath.AddUniqueDynamic(this, &AConstBattleSector::popMonster);
		if (EncounterSightRadius > 0.f)
		{
			if (auto* AC = Cast<AMonsterAIController>(Child->GetController()))
			{
				AC->OverrideSightRadius(EncounterSightRadius);
			}
		}
	}
	Parent->OnMonsterDeath.RemoveDynamic(this, &AConstBattleSector::popMonster);
	Parent->OnEmergeFinished.RemoveDynamic(this, &AConstBattleSector::OnTargetEmergeFinished);
	ActiveMonsters.Remove(Parent);
	EmergeWaitingMonsters.Remove(Parent);
	EmergeFinishedMonsters.Remove(Parent);
	// 부모 교체는 처치가 아니므로 여기서 몬스터 제거 처리나 종료 이벤트를 실행하지 않습니다.
}
