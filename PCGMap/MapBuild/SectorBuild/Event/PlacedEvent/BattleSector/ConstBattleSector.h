#pragma once

#include "CoreMinimal.h"
#include "../../SectorEvent.h"
#include "GameCore/Character/MonsterCharacter.h"
#include "BattlePreEventCompletion.h"
#include "ConstBattleSector.generated.h"

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

USTRUCT(BlueprintType)
struct FBattleMonsterTarget
{
	GENERATED_BODY()

	UPROPERTY(EditInstanceOnly, Category = "Battle")
	TObjectPtr<AMonsterCharacter> Monster;

	UPROPERTY(EditInstanceOnly, Category = "Emerge")
	EEmergeSpawnType SpawnType = EEmergeSpawnType::Floor;

	UPROPERTY(EditInstanceOnly, Category = "Emerge")
	TObjectPtr<UAnimMontage> EmergeMontage;

	UPROPERTY(EditInstanceOnly, Category = "Emerge")
	bool bUseTurnSection = false;

	UPROPERTY(EditInstanceOnly, Category = "Emerge")
	bool bUseTurnRotationWarp = true;
};

// 레벨에 배치한 몬스터의 Emerge와 전투 종료를 관리하는 전투 섹터
UCLASS()
class AConstBattleSector : public AScenePlacedEvent
{
	GENERATED_BODY()
public:

protected:
	virtual void BeginPlay() override;
	virtual void PostLoad() override;
	
public:
	UPROPERTY(EditInstanceOnly, Category = "BattleEvent")
	TArray<FBattleMonsterTarget> BattleTargets;

	/** 기존 레벨의 TargetMonsters 직렬화 데이터를 BattleTargets로 이관하기 위한 호환 필드. */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use BattleTargets instead."))
	TArray<TObjectPtr<AMonsterCharacter>> TargetMonsters;

	//PreEvents 완료를 기다렸다 스폰할지, 스폰과 동시에 재생할지
	UPROPERTY(EditInstanceOnly, Category = "BattleEvent|Emerge")
	EBattleEncounterStartTiming EncounterStartTiming =
		EBattleEncounterStartTiming::AfterPreEventsComplete;

	UPROPERTY(EditInstanceOnly, Category = "BattleEvent|Emerge")
	EBattleCombatActivationPolicy CombatActivationPolicy =
		EBattleCombatActivationPolicy::PerMonsterAfterEmerge;

	/**
	 * 이 섹터 몬스터의 시야 반경 오버라이드(cm). 0이면 몬스터 BP 값을 그대로 쓴다.
	 * 방이 넓어 몬스터가 인스티게이터를 못 보고 타깃을 놓칠 때 올린다.
	 */
	UPROPERTY(EditInstanceOnly, Category = "BattleEvent|Emerge", meta = (ClampMin = "0.0"))
	float EncounterSightRadius = 0.0f;

	virtual void Execute(const UObject* WorldContext = nullptr) override;

protected:
	bool TryCheck() const { return ActiveMonsters.IsEmpty(); }

private :
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

	TArray<TObjectPtr<AMonsterCharacter>> ActiveMonsters;
	TArray<TObjectPtr<AMonsterCharacter>> EmergeFinishedMonsters;
	TSet<int32> CompletedPreEventIds;
	int32 PendingPreEventCount = 0;
	int32 NextPreEventId = 0;
	bool bCombatReleased = false;
	TWeakObjectPtr<AActor> PendingEncounterInstigator;
};
