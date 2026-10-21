#pragma once

#include "CoreMinimal.h"
#include "../../SectorEvent.h"
#include "GameCore/Character/MonsterCharacter.h"
#include "BattlePreEventCompletion.h"
#include "ConstBattleSector.generated.h"

class ASpawnPoint;
class UAnimMontage;

UENUM(BlueprintType)
enum class EBattleEncounterStartTiming : uint8
{
	AfterPreEventsComplete UMETA(DisplayName = "After Pre Events Complete"),
	WithPreEvents UMETA(DisplayName = "With Pre Events")
};

UENUM(BlueprintType)
enum class EBattleCombatActivationPolicy : uint8
{
	PerMonsterAfterEmerge UMETA(DisplayName = "Per Monster After Emerge"),
	AfterAllTargetsEmerged UMETA(DisplayName = "After All Targets Emerged"),
	NeverReleaseForTest UMETA(DisplayName = "Never Release Combat (Test)")
};

/*
 * 레벨에 몬스터를 직접 배치하던 시절의 대상 데이터. 이제 런타임에는 쓰지 않는다.
 * 기존 레벨을 스폰 포인트로 옮기는 변환 스크립트가 이 값을 읽는다. 변환이 끝나면 지운다
 */
USTRUCT(BlueprintType)
struct FBattleMonsterTarget
{
	GENERATED_BODY()


	UPROPERTY(VisibleInstanceOnly, Category = "Battle")
	TObjectPtr<AMonsterCharacter> Monster;

	UPROPERTY(VisibleInstanceOnly, Category = "Emerge")
	EEmergeSpawnType SpawnType = EEmergeSpawnType::Floor;

	UPROPERTY(VisibleInstanceOnly, Category = "Emerge")
	TObjectPtr<UAnimMontage> EmergeMontage;

	UPROPERTY(VisibleInstanceOnly, Category = "Emerge")
	bool bUseTurnSection = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Emerge")
	bool bUseTurnRotationWarp = true;
};

/*
 * 스폰 포인트 묶음으로 전투 하나를 연다. 누가 Execute 했는지는 모른다 — 레벨 시작, 다른 전투의 증원, 컷신 끝 등.
 * 몹은 스폰 포인트가 오브젝트 풀에서 꺼내 오고, 이 섹터는 등장 타이밍·동시 전투 시작·시야·전멸 판정만 맡는다.
 * 전멸하면 PostEvents 를 터뜨린다. WaitEvent 가 이 신호로 완료를 센다
 */
UCLASS()
class AConstBattleSector : public AScenePlacedEvent
{
	GENERATED_BODY()


protected:
	virtual void PostLoad() override;

public:
	// 레벨에 배치한 스폰 포인트. 섹터 조각 BP 안에서는 인스턴스를 찍을 수 없으니 아래 그룹 ID 를 쓴다
	UPROPERTY(EditInstanceOnly, Category = "BattleEvent")
	TArray<TObjectPtr<ASpawnPoint>> SpawnPoints;

	// 같은 조각(레벨 배치면 같은 레벨) 안에서 Group Id 가 이 값인 스폰 포인트도 이 전투에 넣는다
	UPROPERTY(EditAnywhere, Category = "BattleEvent")
	FName SpawnPointGroupId = NAME_None;

	//PreEvents 완료를 기다렸다 스폰할지, 스폰과 동시에 재생할지
	UPROPERTY(EditAnywhere, Category = "BattleEvent|Emerge")
	EBattleEncounterStartTiming EncounterStartTiming = EBattleEncounterStartTiming::AfterPreEventsComplete;

	UPROPERTY(EditAnywhere, Category = "BattleEvent|Emerge")
	EBattleCombatActivationPolicy CombatActivationPolicy = EBattleCombatActivationPolicy::PerMonsterAfterEmerge;

	/**
	 * 이 섹터 몬스터의 시야 반경 오버라이드(cm). 0이면 몬스터 BP 값을 그대로 쓴다.
	 * 방이 넓어 몬스터가 인스티게이터를 못 보고 타깃을 놓칠 때 올린다.
	 */
	UPROPERTY(EditAnywhere, Category = "BattleEvent|Emerge", meta = (ClampMin = "0.0"))
	float EncounterSightRadius = 0.0f;

	// 변환 전용. 런타임에는 쓰지 않는다
	UPROPERTY(VisibleInstanceOnly, Category = "BattleEvent|Legacy",
	          meta = (DeprecatedProperty, DeprecationMessage = "스폰 포인트로 변환 후 삭제"))
	TArray<FBattleMonsterTarget> BattleTargets;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use BattleTargets instead."))
	TArray<TObjectPtr<AMonsterCharacter>> TargetMonsters;

	virtual void Execute(const UObject* WorldContext = nullptr) override;

	bool TracksMonster(AMonsterCharacter* Monster) const
	{
		return ActiveMonsters.Contains(Monster);
	}

	void ReplaceTrackedMonster(AMonsterCharacter* Parent, AMonsterCharacter* Left, AMonsterCharacter* Right);

protected:
	bool TryCheck() const
	{
		return ActiveMonsters.IsEmpty();
	}

private:
	UFUNCTION()
	virtual void popMonster(AMonsterCharacter* popMonster);

	UFUNCTION()
	void OnTargetEmergeFinished(AMonsterCharacter* Monster);

	APawn* FindNearestPlayerPawn() const;

	void StartPreEvents(AActor* EncounterInstigator);

	void ExecutePreEvent(TScriptInterface<ISectorEvent> Event, int32 EventId);

	void OnPreEventCompleted(int32 EventId);

	void StartEncounter(AActor* EncounterInstigator);

	void TryReleaseSynchronizedCombat();

	// 직접 지정한 포인트와 그룹 ID 로 찾은 포인트를 합친다. 중복은 뺀다
	TArray<ASpawnPoint*> GatherSpawnPoints() const;

	TArray<TObjectPtr<AMonsterCharacter>> ActiveMonsters;

	// 동시 전투 시작을 기다리는 몹. 순찰로 나온 몹은 Emerge 가 없어서 빠진다
	TArray<TObjectPtr<AMonsterCharacter>> EmergeWaitingMonsters;

	TArray<TObjectPtr<AMonsterCharacter>> EmergeFinishedMonsters;

	TSet<int32> CompletedPreEventIds;

	int32 PendingPreEventCount = 0;

	int32 NextPreEventId = 0;

	bool bCombatReleased = false;

	TWeakObjectPtr<AActor> PendingEncounterInstigator;
};
