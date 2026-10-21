// 프로젝트 설정의 설명 페이지에 저작권 정보를 입력하세요.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "GameCore/Interface/EncounterTargetInterface.h"
#include "GameCore/Interface/MonsterAttackContext.h"
#include "GameCore/Interface/Poolable.h"
#include "GameCore/Interface/Spawnable.h"
#include "GameCore/Enemy/Patrol/PatrolArea.h"
#include "MonsterCharacter.generated.h"

class UMotionWarpingComponent;
class UMonsterDropComponent;
class UWidgetComponent;
class UUserWidget;
class UAnimMontage;
class USoundBase;
class UCurveFloat;
class AMonsterCharacter;
struct FGameplayEffectContextHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMonsterDeath, AMonsterCharacter*, DeathMonster);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMonsterEmergeFinished, AMonsterCharacter*, EmergeMonster);

UENUM(BlueprintType)
enum class EEmergeSpawnType : uint8
{
	Floor UMETA(DisplayName = "Floor"),
	Wall UMETA(DisplayName = "Wall"),
	Ceiling UMETA(DisplayName = "Ceiling")
};

/** 누적 HitReact를 발생시키는 기준. 선택하지 않은 기준의 값은 에디터에서 비활성화된다. */
UENUM(BlueprintType)
enum class EAccumulatedHitReactCriterion : uint8
{
	Damage UMETA(DisplayName = "Damage Amount"),
	MaxHealthPercent UMETA(DisplayName = "Max Health Percent"),
	HitCount UMETA(DisplayName = "Hit Count")
};

// 스폰 포인트의 Emerge 방식이 넘기는 등장 연출 설정
USTRUCT(BlueprintType)
struct FMonsterEmergeParams
{
	GENERATED_BODY()


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emerge")
	EEmergeSpawnType SpawnType = EEmergeSpawnType::Floor;

	// 비우면 몹의 기본 Emerge 몽타주
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emerge")
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emerge")
	bool bUseTurnSection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emerge")
	bool bUseTurnRotationWarp = true;
};

UCLASS()
class TEAMPROJECT_FIN_API AMonsterCharacter : public ABaseCharacter,
                                              public IEncounterTargetInterface,
                                              public ISpawnable,
                                              public IMonsterAttackContext,
                                              public IPoolable
{
	GENERATED_BODY()


public:
	AMonsterCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UMotionWarpingComponent* GetMotionWarpingComponent() const
	{
		return MotionWarpingComponent;
	}

	UMonsterDropComponent* GetDropComponent() const
	{
		return DropComponent;
	}

	UWidgetComponent* GetMonsterHealthBarComponent() const
	{
		return MonsterHealthBarComponent;
	}

	bool ShouldUseMonsterStatUI() const
	{
		return bUseMonsterStatUI;
	}

	void SetUseMonsterStatUI(bool bUse);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_UseMonsterStatUI, Category = "UI",
	          meta = (DisplayName = "Use Monster Stat UI"))
	bool bUseMonsterStatUI = true;

	UFUNCTION()
	void OnRep_UseMonsterStatUI();

	void SetPossessionPresentationHidden(bool bInHidden);

	EEmergeSpawnType GetEmergeSpawnType() const
	{
		return EmergeSpawnType;
	}

	float GetWeakspotMultiplier() const
	{
		return WeakspotMultiplier;
	}

	float GetStaggerResistance() const
	{
		return StaggerResistance;
	}

	UAnimMontage* GetEmergeMontageOverride() const
	{
		return EmergeMontageOverride.Get();
	}

	bool ShouldUseEmergeTurnSection() const
	{
		return bUseEmergeTurnSection;
	}

	bool ShouldUseEmergeTurnRotationWarp() const
	{
		return bUseEmergeTurnRotationWarp;
	}

	/** Battle sector가 이번 전투에 사용할 등장 설정을 적용한다. */
	void ConfigureBattleEmerge(EEmergeSpawnType InEmergeSpawnType, UAnimMontage* InEmergeMontage,
	                           bool bInUseTurnSection, bool bInUseTurnRotationWarp);

	// --- 몬스터 공격 컨텍스트 인터페이스 -------------------------------------------

	virtual void SetCurrentAttackTag(const FGameplayTag& InAttackTag) override
	{
		CurrentAttackTag = InAttackTag;
	}

	virtual FGameplayTag GetCurrentAttackTag() const override
	{
		return CurrentAttackTag;
	}

	// ---------------------------------------------------------------------

	void SetWaitForBattleCombatRelease(bool bInWait)
	{
		bWaitForBattleCombatRelease = bInWait;
	}

	bool ShouldWaitForBattleCombatRelease() const
	{
		return bWaitForBattleCombatRelease;
	}

	void NotifyEmergeFinished();

	void ReleaseBattleCombat();

	/** 등장 GA가 끝날 때 RequestEncounter에서 선점한 무적 태그를 반환한다. */
	void ReleasePendingEmergeInvincibility();

	/** 인카운터 볼륨이 호출하는 일반 몬스터 등장 진입점. */
	virtual bool RequestEncounter_Implementation(AActor* EncounterInstigator) override;

	/** 사망 시작 시 몬스터별 처리를 수행 */
	virtual void BeginDeath() override;

	/** 사망 연출 종료 후 몬스터별 후처리를 수행 */
	virtual void FinishDeath() override;

	/** 풀 재사용으로 되살아날 때 남은 사망 연출을 걷어낸다 */
	virtual void Revive() override;

	/** 피격 시 몬스터 넉백에 대한 정책 결정 수행 */
	virtual void HandleDamageHitReact(float Damage, AActor* InstigatorActor,
	                                  const FGameplayEffectContextHandle& EffectContext);

	/** 실제 적용 피해량과 공격자를 AI 컨트롤러에 알린다. 타깃 재평가의 즉시 트리거다. */
	void NotifyDamagedBy(AActor* DamageInstigator, float AppliedDamage);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//빌드용 구현=================================================
	virtual void Spawn() override;

	virtual void Hide() override;

	//============================================================

	// --- 스폰 시스템 -----------------------------------------------------

	/*
	 * 숨은 채 대기하다 Emerge 로 튀어나와 바로 전투한다. 서버 전용.
	 * 보스처럼 등장 연출이 다른 몹은 RequestEncounter 오버라이드가 그대로 이어받는다
	 */
	virtual bool PlayEmerge(const FMonsterEmergeParams& Params, AActor* EncounterInstigator);

	/*
	 * Emerge 없이 보이는 채로 나와 영역을 순찰한다. 서버 전용.
	 * 플레이어는 시야로 발견해야 싸운다. 영역을 보관하는 일은 순찰을 아는 파생 클래스가 한다
	 */
	virtual void StartPatrol(const TScriptInterface<IPatrolArea>& Area);

	/*
	 * 전투하다 플레이어를 놓쳤을 때 돌아갈 영역만 넘긴다. 보이게 하거나 깨우지는 않는다.
	 * 영역을 보관하는 일은 순찰을 아는 파생 클래스가 한다
	 */
	virtual void SetPatrolArea(const TScriptInterface<IPatrolArea>& Area)
	{
	}

	// 지금 타깃을 쫓고 있는가. 거리 기반 반환을 미룰지 판단한다
	bool IsEngagedInCombat() const;

	/*
	 * 모든 플레이어가 멀리 있을 때 그 자리에 재운다. 서버 전용.
	 * AI(StateTree·퍼셉션)·이동·애니메이션·Tick 을 멈추고 체력과 전투 상태는 그대로 둔다. 보이는 상태도 유지한다
	 */
	void SetDistanceSleep(bool bSleep);

	bool IsDistanceSleeping() const
	{
		return bDistanceSleeping;
	}

	// 풀링 인터페이스
	virtual void OnAcquired(const FTransform& SpawnTransform) override;

	virtual void OnReleased() override;

	// 사망 연출이 끝난 뒤 시체를 남기는 시간. 풀 몹은 이만큼 지난 뒤 반환된다
	float GetCorpseLifeSpan() const
	{
		return CorpseLifeSpan;
	}

	/*
	 * 평소 사망 GA 경로(Event.Death)로 죽이되 골드·무기 보상은 주지 않는다. 서버 전용.
	 * 보스가 죽을 때 남은 애드를 정리하는 데 쓴다. 이미 죽었거나 죽는 중이면 아무것도 하지 않는다
	 */
	void KillWithoutReward();

	// ---------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Death")
	FOnMonsterDeath OnMonsterDeath;

	UPROPERTY(BlueprintAssignable, Category = "Emerge")
	FOnMonsterEmergeFinished OnEmergeFinished;

protected:
	/*
	 * 지금 시전 중인 공격 태그. 어빌리티가 활성화될 때 채우고 끝날 때 비운다.
	 *
	 * 복제하지 않는다. 데미지 판정은 서버만 하므로 클라이언트가 알 필요가 없다.
	 */
	FGameplayTag CurrentAttackTag;

	virtual void BeginPlay() override;

	/** 배틀 셀렉터가 이번 전투 시작 직전에 적용하는 등장 설정. */
	UPROPERTY(meta = (DeprecatedProperty,
	                  DeprecationMessage = "Configure Emerge from the battle sector target instead."))
	EEmergeSpawnType EmergeSpawnType = EEmergeSpawnType::Floor;

	UPROPERTY(meta = (DeprecatedProperty,
	                  DeprecationMessage = "Configure Emerge from the battle sector target instead."))
	TObjectPtr<UAnimMontage> EmergeMontageOverride;

	UPROPERTY(meta = (DeprecatedProperty,
	                  DeprecationMessage = "Configure Emerge from the battle sector target instead."))
	bool bUseEmergeTurnSection = false;

	UPROPERTY(meta = (DeprecatedProperty,
	                  DeprecationMessage = "Configure Emerge from the battle sector target instead."))
	bool bUseEmergeTurnRotationWarp = true;

	bool bWaitForBattleCombatRelease = false;

	// 공개와 GA_Emerge 활성화 사이의 피격 공백을 막기 위해 직접 추가한 태그인지 추적한다.
	bool bHasPendingEmergeInvincibility = false;

	void ApplyMonsterInitialStats();

	/** 기존 BP 행 적용 뒤, Definition DA가 지정한 몬스터 행으로 최종 값을 덮어쓴다. */
	void ApplyMonsterInitialStatsFromRow(const FDataTableRowHandle& StatsRow);

	/** DT의 MaxHealth에 곱하는 인원별 배율. 서버에서 초기 스탯을 적용할 때만 계산한다. */
	virtual float GetMaxHealthScale() const;

	/** 새 Framework의 독립 전투 테스트가 부모 Encounter 표시 상태를 안전하게 해제할 때 사용합니다. */
	void SetEncounterPresentationVisibleForFrameworkTest(bool bVisible);

	/*
	 * 풀에서 다시 꺼낼 때 새로 스폰된 것처럼 되돌린다. 사망 상태와 태그는 부모가 정리한 뒤에 불린다.
	 * 스탯·특성처럼 종류마다 다른 초기화는 파생 클래스가 덧붙인다
	 */
	virtual void ResetForReuse();

	// 풀이 내준 몹인가. 죽어도 액터를 지우지 않고 스폰 포인트가 반환한다
	bool bPooled = false;

	/*
	 * 사망 처리가 끝난 뒤 액터를 제거하기까지의 시간(초).
	 *
	 * 종류마다, 배치마다 다를 수 있어 Definition DA가 아니라 캐릭터가 들고 있다.
	 * 시체를 남기지 않는 지금 동작을 유지하려면 기본값 그대로 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster|Death", meta = (ClampMin = "0.0"))
	float CorpseLifeSpan = 0.1f;

private:
	/** 지정된 시간 동안 받은 피해를 누적하며, 누적 피해가 임계치에 도달하면 일반 HitReact 이벤트를 전송하고 누적 피해와 타이머를 초기화한다. */
	void AccumulateHitReactDamage(float Damage, AActor* InstigatorActor,
	                              const FGameplayEffectContextHandle& EffectContext);

	/** 몬스터의 ASC에 일반 HitReact 이벤트를 전송한다. */
	void SendHitReactEvent(AActor* InstigatorActor, const FGameplayEffectContextHandle& EffectContext, float Damage);

	/** 누적 피해 제한 시간이 만료됐을 때 현재까지 누적된 피해량을 초기화 */
	void ResetAccumulatedHitReactDamage();

	void ApplyEncounterPresentation(bool bVisible);

	void InitializeMonsterHealthBar();

	UFUNCTION()
	void OnRep_EncounterPresentationVisible();

	UFUNCTION()
	void OnRep_DistanceSleeping();

	/*
	 * 숨었거나 자는 몹은 Tick 을 전부 끈다 — 액터, 메시(애니메이션), 이동.
	 * 풀에서 쉬는 몹과 멀리서 자는 몹이 매 프레임 비용을 쓰지 않게 한다. 모든 머신에서 돈다
	 */
	void UpdateTickState();

	// 마지막으로 적용한 표시 상태. 멀티캐스트가 복제 값보다 먼저 올 수 있어 따로 든다
	bool bPresentationShown = false;

	UFUNCTION()
	void OnRep_PossessionPresentationHidden();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetEncounterPresentation(bool bVisible);

public:
	/*
	 * 서버 전용 GA가 낸 소리를 모든 머신에서 재생한다.
	 *
	 * 몬스터 GA는 서버에서만 돌기 때문에 GA 안에서 로컬로 재생하면 호스트만 듣는다.
	 * 연출용이라 한두 번 빠져도 게임에 영향이 없어 Unreliable 로 보낸다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySoundAtLocation(USoundBase* Sound, FVector_NetQuantize Location, float VolumeMultiplier);

protected:
	// 근접 공격 몽타주가 타겟 위치로 루트 모션을 보정할 때 쓴다. 몬스터 전용, 플레이어에는 없음.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;

	// 피해 기여도를 모아 두었다가 사망 시 골드를 나눠 준다. 아이템 드랍 요청도 여기서 나간다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drop", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMonsterDropComponent> DropComponent;

	/** 머리 위 체력바. 서버에는 렌더링하지 않고, 각 클라이언트가 복제된 ASC 체력으로 갱신한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> MonsterHealthBarComponent;

	/** UMonsterStatusWidget을 부모로 하는 WBP를 지정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UUserWidget> MonsterHealthBarWidgetClass;

	/** 카메라와 이 거리보다 멀면 체력바를 숨긴다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	float MaxHealthBarDistance = 10000.0f;

	/** 체력바 거리/가림 판정 주기(초). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI")
	float HealthBarCheckInterval = 0.15f;

	friend class UMonsterHealthBarVisibilitySubsystem;

	/** GA_HitReact 사용 위한 몬스터 셋팅 값 */

	/** 피해를 받았을 때 일반 HitReact를 사용할지 여부 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact")
	bool bEnableHitReact = true;

	/** 누적 피해가 임계치를 넘었을 때만 HitReact를 실행할지 여부 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact")
	bool bUseAccumulatedHitReact = false;

	/** 누적 HitReact의 판정 기준. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact",
	          meta = (EditCondition = "bUseAccumulatedHitReact"))
	EAccumulatedHitReactCriterion AccumulatedHitReactCriterion = EAccumulatedHitReactCriterion::Damage;

	/** Damage Amount 기준일 때 필요한 누적 피해량. */
	UPROPERTY(
	    EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact",
	    meta = (EditCondition =
	                "bUseAccumulatedHitReact && AccumulatedHitReactCriterion == EAccumulatedHitReactCriterion::Damage",
	            ClampMin = "0.0"))
	float AccumulatedHitReactThreshold = 100.0f;

	/** Max Health Percent 기준일 때 필요한 누적 피해량의 최대 체력 대비 비율. */
	UPROPERTY(
	    EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact",
	    meta =
	        (EditCondition =
	             "bUseAccumulatedHitReact && AccumulatedHitReactCriterion == EAccumulatedHitReactCriterion::MaxHealthPercent",
	         ClampMin = "0.0", ClampMax = "100.0"))
	float AccumulatedHitReactMaxHealthPercentThreshold = 10.0f;

	/** Hit Count 기준일 때 필요한 누적 피격 횟수. 피해량과 무관하게 유효 피격 1회마다 증가한다. */
	UPROPERTY(
	    EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact",
	    meta =
	        (EditCondition =
	             "bUseAccumulatedHitReact && AccumulatedHitReactCriterion == EAccumulatedHitReactCriterion::HitCount",
	         ClampMin = "1", UIMin = "1"))
	int32 AccumulatedHitReactHitCountThreshold = 3;

	/** 최초 피격부터 피해를 누적하는 시간 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|HitReact",
	          meta = (EditCondition = "bUseAccumulatedHitReact", ClampMin = "0.1"))
	float AccumulatedHitReactWindow = 3.0f;

	/** 현재 누적 피해 제한 시간 안에 받은 총 피해량 */
	float AccumulatedHitReactDamage = 0.0f;

	/** 현재 누적 창에서 들어온 유효 피격 횟수. */
	int32 AccumulatedHitReactHitCount = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Monster|Attributes",
	          meta = (RowType = "/Script/TeamProject_Fin.MonsterInitStats"))
	FDataTableRowHandle MonsterStatsRow;

	/** X=접속 인원, Y=최대 체력 배율. 비어 있으면 배율 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Attributes")
	TObjectPtr<UCurveFloat> MaxHealthScaleByPlayerCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Monster|Attributes", meta = (AllowPrivateAccess = "true"))
	float WeakspotMultiplier = 1.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Monster|Attributes", meta = (AllowPrivateAccess = "true"))
	float StaggerResistance = 1.0f;

	/** 누적 피해 제한 시간이 만료될 때 피해량을 초기화하는 타이머 */
	FTimerHandle AccumulatedHitReactTimerHandle;

	UPROPERTY(ReplicatedUsing = OnRep_EncounterPresentationVisible)
	bool bEncounterPresentationVisible = false;

	UPROPERTY(ReplicatedUsing = OnRep_PossessionPresentationHidden)
	bool bPossessionPresentationHidden = false;

	// 클라도 멀리서 자는 몹의 애니메이션·이동 Tick 을 끄도록 복제한다
	UPROPERTY(ReplicatedUsing = OnRep_DistanceSleeping)
	bool bDistanceSleeping = false;
};
