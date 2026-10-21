// 프로젝트 설정의 설명 페이지에 저작권 정보를 입력하세요.

#pragma once

#include "CoreMinimal.h"
#include "GameCore/AI/BaseAIController.h"
#include "GameplayTagContainer.h"
#include "GameCore/Combat/IncomingAttackReceiver.h"
#include "MonsterAIController.generated.h"

class UAISenseConfig_Sight;
class UMonsterTargetSelector;
struct FAIStimulus;

/** 공격자별로 보관하는 정규화 전 피해 Threat 값입니다. */
struct FMonsterDamageThreatEntry
{
	float Value = 0.0f;

	double LastUpdatedTime = -1.0;
};
/**
 * 시야 퍼셉션을 소유하고 처리하는 몹 전용 컨트롤러.
 *
 * StateTree 의 Evaluator 는 이 컨트롤러가 이미 정리한 값을 읽기만 한다.
 */

UCLASS()
class TEAMPROJECT_FIN_API AMonsterAIController : public ABaseAIController, public IIncomingAttackReceiver
{
	GENERATED_BODY()


public:
	AMonsterAIController();

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	/*
	 * 시야 쿼리를 만들 때 엔진이 부르는 피아 판정. 역할 태그(Character.Player)를 가진 대상만 적으로 본다.
	 * 몹끼리 서로를 시야 대상으로 삼으면 쿼리가 몹 수의 제곱으로 늘어 플레이어 갱신이 늦어지므로 여기서 막는다.
	 */
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	UFUNCTION(BlueprintPure, Category = "AI|Perception")

	AActor* GetPerceivedTarget() const
	{
		return TargetActor;
	}

	/** 기억 중인 타깃과 실제로 보이는 타깃을 구분한다. */
	bool IsTargetVisible(const AActor* Target) const;

	bool CanAttackTarget(const AActor* Target) const;

	bool GetPursuitTargetLocation(const AActor* Target, FVector& OutLocation) const;

	UFUNCTION(BlueprintPure, Category = "AI|Perception")

	bool HasPerceivedTarget() const
	{
		return bHasTarget;
	}

	/** 거리 트리거가 전달한 대상을 최초 타깃으로 저장하고 등장 인카운터 이벤트를 State Tree에 전송한다. */
	UFUNCTION(BlueprintCallable, Category = "AI|Encounter")
	void RequestEncounter(AActor* EncounterTarget);

	/** 전투 섹터가 넓은 방에서 시야를 넓히기 위해 런타임에 덮어쓴다. 0 이하면 무시. */
	void OverrideSightRadius(float NewSightRadius);

	/*
	 * 켜면 시야와 무관하게 전투 가능한 플레이어 전원을 후보로 둔다. 보스·보스 애드처럼 끝까지 쫓아야 하는 적용.
	 * ScoringRange 가 0 보다 크면 셀렉터의 거리 점수 범위도 덮어쓴다. 누구를 칠지는 여전히 셀렉터가 고른다.
	 */
	void SetTrackAllPlayers(bool bEnable, float ScoringRange = 0.0f);

	void RestoreDefaultTargetTracking();

	// Sentinel Perception Evaluator가 실제로 복사한 값을 화면 디버그에 전달한다.
	void UpdatePerceptionEvaluatorDebug(AActor* InTargetActor, bool bInHasTarget);

	// 실제 공격 원점이 왼쪽이면 음수, 오른쪽이면 양수. 회피 방향 선택에 쓴다.
	UFUNCTION(BlueprintPure, Category = "AI|Perception")

	float GetGunfireDirectionSign() const
	{
		return GunfireDirectionSign;
	}

	// 실제 공격 판정에 포함되고 회피 조건을 만족한 경우 StateTree에 회피 이벤트를 보낸다.
	virtual bool NotifyIncomingAttack(AActor* Attacker, const FVector& AttackOrigin) override;

	/*
	 * 피해를 입은 직후 호출한다. 지금 타깃이 아닌 상대에게 맞았다면 즉시
	 * 재평가해, 뒤에서 때리는 플레이어를 다음 주기까지 무시하지 않게 한다.
	 */
	void NotifyDamagedBy(AActor* DamageInstigator, float AppliedDamage);

	/*
	 * InWindow 초 안에 이 상대에게 맞았는가.
	 *
	 * 누적 위협도가 아니라 "최근에 맞았는지"만 본다. 누적을 쓰면 초반에 크게
	 * 때린 한 명에게 전투 내내 고정되는데, 그건 코옵에서 벌칙이 된다.
	 */
	bool WasRecentlyDamagedBy(const AActor* DamageInstigator, float InWindow) const;

	/** 실제 적용 피해량으로 쌓인 Threat를 [0, 1]로 정규화해 반환합니다. */
	float GetDamageThreatNormalized(const AActor* DamageInstigator) const;

	// 지금 상태가 회피(Evade)로 인터럽트 가능한지. 공격 실행 중엔 false.
	UFUNCTION(BlueprintPure, Category = "AI|Combat")

	bool IsInterruptible() const
	{
		return bIsInterruptible;
	}

	/** 실제 공격 위협을 감지했을 때 evade를 시작할 확률. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Combat|Evade",
	          meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EvadeChance = 0.5f;

	// 각 상태 진입 시 호출해서 지금 상태의 인터럽트 가능 여부를 갱신한다.
	UFUNCTION()

	void SetInterruptible(bool bValue)
	{
		bIsInterruptible = bValue;
	}

	/*
	 * 켜면 UpdateMovement가 이동을 건너뛴다.
	 * 공격 등 커밋된 행동을 실행하는 동안 GA가 이걸 걸었다 풀었다 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Movement")
	void SetMovementLocked(bool bLocked);

	UFUNCTION(BlueprintPure, Category = "AI|Movement")

	bool IsMovementLocked() const
	{
		return bMovementLocked;
	}

	/**
	 * 끄면 타깃을 향해 걸어가지 않는다. 순간이동으로만 이동하는 몬스터용이다.
	 * 일시적으로 막고 싶을 때는 이 값 대신 State.AI.MovementDisabled 태그를 쓴다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "AI|Movement")
	bool bEnablePursuitMovement = true;

	/*
	 * BehaviorDecision에서 뽑힌 행동 태그. FSTE_MonsterCombatState가 매 틱
	 * 이 값을 읽어서 StateTree 어디서든 바인딩 가능하게 노출한다.
	 * 형제 State끼리는 서로의 Task 출력을 못 보기 때문에, 최상위 Evaluator가
	 * 읽는 이 컨트롤러를 경유해야 한다.
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")

	FGameplayTag GetSelectedAction() const
	{
		return SelectedAction;
	}

	UFUNCTION()

	void SetSelectedAction(FGameplayTag InAction)
	{
		SelectedAction = InAction;
	}

	/*
	 * 자신 주변 Radius 안에서 RequiredTag를 가진 액터 중 가장 가까운 것을 찾는다.
	 * 특정 몬스터에 종속되지 않는 범용 탐색 함수 — 몬스터별 GA가 원하는 태그를
	 * 넘겨서 쓴다(예: 빙의 대상 탐색용 Character.Enemy.Ranged).
	 * 자기 자신과 State.Dead 태그가 붙은 액터는 제외한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Perception")
	AActor* FindNearestActorWithTag(FGameplayTag RequiredTag, float Radius) const;

	/** 빙의 전용 대상 탐색. 필수/차단 태그와 대상의 현재 전투 타겟을 함께 검사한다. */
	AActor* FindNearestPossessionTarget(const FGameplayTagContainer& RequiredTags,
	                                    const FGameplayTagContainer& BlockedTags, AActor* ExpectedTarget,
	                                    float Radius) const;

	/** 몬스터 사망 시 이동, 인지 및 StateTree 실행을 중지 */
	void StopAIForDeath();

	/** 이동, 인지, 타깃, StateTree 를 모두 멈춘다. 사망과 풀 반환이 같이 쓴다 */
	void SuspendAI(const TCHAR* Reason);

	/**
	 * 멈춘 AI 를 새로 스폰된 것처럼 켠다. 이동 잠금 상태로 시작하고 StateTree 는 루트부터 다시 돈다.
	 * 등장 방식이 정해지는 시점에 부른다 — 그때의 태그로 StateTree 가 가지를 고른다
	 */
	void ResumeAI();

	/*
	 * 모든 플레이어가 멀리 있을 때 판단을 멈춘다. StateTree 는 일시정지라 깨우면 하던 상태에서 이어간다.
	 * 퍼셉션과 이동, 컨트롤러 Tick 도 같이 멈춘다
	 */
	void SetSleeping(bool bSleep);

	/** StateTree에 이벤트(태그)를 전송*/
	UFUNCTION(BlueprintCallable, Category = "AI|StateTree")
	void SendAIStateTreeEvent(FGameplayTag EventTag);

protected:
	// 아직 못 본 상대를 처음 발견하는 거리
	UPROPERTY(EditDefaultsOnly, Category = "Perception")
	float SightRadius = 1000.0f;

	/*
	 * 한 번 본 상대를 계속 인지하는 거리. SightRadius 보다 커야 경계에서 깜빡이지 않는다.
	 * 네비메시가 없는 곳까지 쫓지 않도록 스폰 활성화 거리(인보커 생성 반경 - Activation Margin) 안쪽으로 둔다
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception")
	float LoseSightRadius = 2500.0f;

	// 정면 기준 좌우 편각이다. 180 이면 전방위(360도)를 본다
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float PeripheralVisionAngle = 180.0f;

	/*
	 * 시선이 끊긴 뒤에도 타깃으로 유지하는 시간(초). 문틀·벽 모서리에 잠깐 가려졌다고 추격이 끊기지 않게 한다.
	 * 보이는 후보가 하나도 없을 때만 쓰므로, 눈앞에 보이는 다른 플레이어가 있으면 그쪽이 우선한다.
	 * 사망·은신 필터는 그대로 적용된다. 퍼셉션의 기억 수명(MaxAge)으로도 쓰인다. 0이면 시선이 끊기는 즉시 놓친다
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0", Units = "s"))
	float SightMemoryDuration = 5.0f;

	// AI Sight의 중심 방향을 상하로 보정한다. 음수는 아래, 양수는 위를 향한다.
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "-89.0", ClampMax = "89.0"))
	float SightPitchOffset = 0.0f;

	// 타깃과 현재 State Tree 상태/Task를 화면에 표시한다. 개발 빌드에서만 동작한다.
	UPROPERTY(EditDefaultsOnly, Category = "AI|Debug")
	bool bShowAIDebugMessages = true;

	/*
	 * 새 후보가 지금 타겟보다 이 비율만큼 더 가까워야 갈아탄다.
	 *
	 * 0.8이면 "새 후보 거리 < 현재 타겟 거리 * 0.8"일 때만 전환.
	 * 두 플레이어가 거리가 비슷할 때 타겟이 계속 바뀌는 걸 막는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TargetSwitchMargin = 0.8f;

	/*
	 * 타깃 재평가 주기(초).
	 *
	 * 퍼셉션 콜백은 감지 상태가 바뀔 때만 오므로, 타깃이 계속 보이는 채로
	 * 죽거나 은신하면 갱신 기회가 없다. 주기 평가가 그 구멍을 메운다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.05"))
	float TargetEvaluationInterval = 0.5f;

	/*
	 * 같은 타깃을 유지할 수 있는 최대 시간(초).
	 *
	 * 이 시간을 넘기면 유효 후보가 둘 이상일 때 현재 타깃을 후보에서 빼고
	 * 다시 고른다. 한 플레이어에게 영구히 고정되는 상황을 막는 장치다.
	 * 0이면 강제 전환을 하지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float MaxTargetHoldTime = 15.0f;

	/*
	 * 등 뒤에서 맞았을 때 그 상대를 후보로 인정하는 시간(초).
	 *
	 * 시야각(PeripheralVisionAngle)을 360도로 열면 측면·후면 우회 플레이가
	 * 통째로 사라진다. 각도는 그대로 두고, 실제로 맞았을 때만 예외로 후보에
	 * 넣어 "때리면 돌아보지만 조용히 접근하면 못 본다"를 만든다.
	 * 0이면 예외를 두지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float DamagedTargetMemoryDuration = 3.0f;

	/*
	 * 조우로 받은 상대를 시야 없이도 후보로 인정하는 시간(초).
	 *
	 * 풀에서 막 꺼낸 몹은 퍼셉션이 아직 아무도 못 봤다. 이 예외가 없으면
	 * 조우 직후 재평가가 인스티게이터를 바로 지워서, 타깃이 필요한 등장(Emerge)이
	 * 영영 시작되지 못하고 그 자리에 굳는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0", Units = "s"))
	float EncounterTargetGraceTime = 5.0f;

	/*
	 * 피격 이력을 들고 있는 시간(초).
	 *
	 * 이 기록을 읽는 곳이 둘이고(후보 예외, 셀렉터의 가산점) 각자 다른 창을
	 * 쓴다. 저장 수명을 그 둘과 분리해 두어야, 짧은 쪽 기준으로 지워져서
	 * 긴 쪽이 기록을 잃는 일이 없다. 두 창보다 넉넉하게 잡는다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Perception", meta = (ClampMin = "0.0"))
	float DamageMemoryRetention = 10.0f;

	/*
	 * 후보 중 누구를 칠지 정하는 정책.
	 *
	 * 컨트롤러는 언제 평가할지와 후보를 모으는 일만 맡고 선정 규칙은 모른다.
	 * 몹 종류별로 다른 셀렉터를 끼워 성격을 바꾼다. 비어 있으면
	 * UWeightedTargetSelector가 기본으로 생성된다.
	 */
	UPROPERTY(EditDefaultsOnly, Instanced, Category = "AI|Targeting")
	TObjectPtr<UMonsterTargetSelector> TargetSelector = nullptr;

	/** Threat가 절반으로 줄어드는 시간(초)입니다. 피해를 멈춘 타깃은 점차 우선순위를 잃습니다. */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Targeting|Threat", meta = (ClampMin = "0.01"))
	float DamageThreatHalfLife = 10.0f;

	/** 공격자 한 명이 보관할 수 있는 정규화 전 Threat 최대치입니다. */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Targeting|Threat", meta = (ClampMin = "0.01"))
	float DamageThreatCap = 1.0f;

	/*
	 * 지금 시야에 잡힌 액터와 최근 나를 때린 상대 중 전투 대상 조건을 만족하는 것을 모은다.
	 * 파생 컨트롤러는 후보를 더할 수 있다. 부모를 먼저 불러야 피격 기록 정리가 빠지지 않는다.
	 */
	virtual void GatherTargetCandidates(TArray<AActor*>& OutCandidates) const;

	// 일반 전투에서 몬스터가 추적할 수 있는 대상인지 판정한다.
	// 시야, 인카운터 트리거 모두 같은 필터를 사용해 아군 몬스터를 타깃으로 잡지 않게 한다.
	bool IsValidCombatTarget(const AActor* Candidate) const;

private:
	struct FLastSeenTarget
	{
		FVector Location = FVector::ZeroVector;
		double Time = 0.0;
	};

	TMap<TWeakObjectPtr<AActor>, FLastSeenTarget> LastSeenTargets;

	mutable TSet<TWeakObjectPtr<AActor>> ExhaustedSearchTargets;

	double LastSeenArrivalTime = -1.0;

	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = "0.0", Units = "s"))
	float LastSeenSearchWait = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = "1.0", Units = "s"))
	float LastSeenPursuitTimeout = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "AI|Perception", meta = (ClampMin = "1.0", Units = "cm"))
	float LastSeenAcceptanceRadius = 100.0f;

	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	void ComputeGunfireDirection(const FVector& StimulusLocation);

	/*
	 * 타깃 선정의 유일한 진입점.
	 *
	 * 주기 틱과 즉시 트리거(감지 변화, 피격, 인카운터)가 모두 이 함수를 부른다.
	 * 규칙을 한 곳에 모아두면 "왜 이 타깃인지"를 로그 한 줄로 설명할 수 있다.
	 */
	void EvaluateTarget();

	// 셀렉터를 돌려준다. BP에서 지정하지 않았으면 기본 정책을 만들어 채운다.
	UMonsterTargetSelector* GetTargetSelector();

	// 타깃을 교체하고 유지 시간을 초기화한다. 전환 사유를 로그에 남긴다.
	void SetCurrentTarget(AActor* NewTarget, const TCHAR* Reason);

	// 현재 타깃을 몇 초째 유지하고 있는가. 타깃이 없으면 0.
	float GetTargetHoldTime() const;

	/*
	 * 거리에 따라 Strafe/Fly 중 하나를 매 프레임 계속 실행한다.
	 * StateTree Task가 아니라 여기 있는 이유: 이동은 "끝나는 개념"이
	 * 없는 지속 동작이라 State의 완료 판정과 안 맞는다.
	 */
	void UpdateMovement(float DeltaSeconds);

	// 추격 경로를 따라가는 중일 때만 끊는다. 매 틱 StopMovement가 불리는 것을 막는다.
	void StopPursuitPath(class UMonsterMovementComponent* MovementComponent);

	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(Transient)
	TObjectPtr<AActor> TargetActor = nullptr;

	bool bHasTarget = false;

	// 다음 주기 평가 시각. BeginPlay에서 몬스터마다 다른 오프셋을 준다.
	double NextTargetEvaluationTime = 0.0;

	// 현재 타깃을 잡은 시각. 유지 시간 상한 판정에 쓴다.
	double CurrentTargetAcquiredTime = -1.0;

	/*
	 * 상대별 마지막 피격 시각. 최근 피격 가산점과 후보 예외의 근거다.
	 * 조회할 때 만료 항목을 걷어내므로 const 함수에서 쓰도록 mutable로 둔다.
	 */
	mutable TMap<TWeakObjectPtr<AActor>, double> LastDamagedTimeByInstigator;

	/** 공격자별 실제 적용 피해량 Threat. 값은 피해량 / 몬스터 최대 체력의 누적입니다. */
	mutable TMap<TWeakObjectPtr<AActor>, FMonsterDamageThreatEntry> DamageThreatByInstigator;

	// 조우 인스티게이터와 후보 인정이 끝나는 시각. 보이는 후보가 없을 때만 쓴다
	TWeakObjectPtr<AActor> EncounterFallbackTarget;

	double EncounterFallbackUntil = -1.0;

	TWeakObjectPtr<AActor> DebugEvaluatorTargetActor;

	bool bDebugEvaluatorHasTarget = false;

	uint64 DebugEvaluatorTickCount = 0;

	double DebugEvaluatorLastTickTime = -1.0;

	// StateTree 상태가 바뀔 때만 디버그 문자열을 기록한다. 매 프레임 로그를 남기지 않는다.
	FString LastStateTreeDebugMessage;

	float GunfireDirectionSign = 0.0f;

	// 기본값 true. 이동/포지셔닝 계열 상태는 true, 커밋된 공격 실행 중엔 false.
	bool bIsInterruptible = true;

	bool bMovementLocked = false;

	// 직전 틱의 ActionCommitted 여부. 행동이 시작된 틱에만 이동을 끊기 위해 둔다.
	bool bWasActionCommitted = false;

	// 직전 틱에 추격할 타깃이 있었는가. 타깃을 잃은 틱에만 이동을 끊기 위해 둔다.
	bool bHadTargetLastTick = false;

	bool bSleeping = false;

	// SetTrackAllPlayers 로 켠다. 풀 몹은 스폰 포인트가 꺼낼 때마다 다시 정한다
	bool bTrackAllPlayers = false;

	// 전원 추적 중 셀렉터에 넘길 거리 점수 범위. 0 이면 셀렉터 자체 값을 쓴다
	float TrackScoringRange = 0.0f;

	FGameplayTag SelectedAction;
};
