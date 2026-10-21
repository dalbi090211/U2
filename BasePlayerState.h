#pragma once

#include "GameFramework/PlayerState.h"
#include "GameCore/UI/DLPlayerLoadOut.h"
#include "GameCore/Weapon/Data/WeaponTypes.h"
#include "GameCore/Match/RunStatistics.h"

#include "BasePlayerState.generated.h"

class UDLInventoryComponent;

// 팀원 UI용 체력 사본. 폰은 렐러번시로 클라에서 사라질 수 있지만 PlayerState는 항상 복제된다.
USTRUCT(BlueprintType)
struct FDLPlayerVitals
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float Health = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MaxHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float HealthCapMultiplier = 1.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPlayerVitalsChanged, float, Health, float, MaxHealth, float, HealthCapMultiplier);

// Delta는 이번 변화량이다. 획득 연출(+50 표시)이 필요한 UI가 쓴다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGoldChanged, int32, NewGold, int32, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRunStatisticsChanged);
USTRUCT(BlueprintType)
struct FGeneratedWeaponRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Generation")
	int32 Sequence = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Generation")
	EWeaponGenerationSource Source = EWeaponGenerationSource::Shop;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Generation")
	EGunRarity Rarity = EGunRarity::Common;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Generation")
	EWeaponCategory WeaponCategory = EWeaponCategory::Rifle;
};

USTRUCT(BlueprintType)
struct FGeneratedWeaponRarityCounts
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Common = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Uncommon = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Epic = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 Legendary = 0;
};

USTRUCT(BlueprintType)
struct FGeneratedWeaponCategoryCounts
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Rifle = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 GrenadeLauncher = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 SniperRifle = 0;
};

UCLASS()
class ABasePlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	ABasePlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 맵 이동 후 로드아웃 유지 여부 로그
	virtual void BeginPlay() override;

	// 심리스 트래블은 PS를 새로 만든다. Loadout을 직접 넘기지 않으면 기본값으로 돌아간다.
	virtual void CopyProperties(APlayerState* NewPlayerState) override;

	UFUNCTION(BlueprintPure, Category = "Match")
	int32 GetRoomJoinOrder() const { return RoomJoinOrder; }

	// 서버에서 최초 입장 시 한 번만 부여한다. 심리스 이동에서는 유지한다.
	void SetRoomJoinOrder(int32 NewOrder);

	// 대기방 복귀 직전에 서버가 호출한다. 이름, 영웅, 입장 순서는 유지한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Match")
	void ResetRunData();

	UFUNCTION(BlueprintPure, Category = "Run Statistics")
	const FRunStatistics& GetRunStatistics() const { return RunStatistics; }

	UPROPERTY(BlueprintAssignable, Category = "Run Statistics")
	FOnRunStatisticsChanged OnRunStatisticsChanged;

	// 서버 기록 전용. 클리어 확정 이후에는 추가 집계하지 않는다.
	void RecordDamageDealt(float ActualDamage, bool bBoss, bool bKilled);
	void RecordDamageTaken(float ActualDamage);
	void RecordTeamRevive();
	void FinalizeRunStatistics(float ClearDurationSeconds);
	void ResetRunStatistics();

	/*
	 * 이 PlayerState가 로컬 플레이어의 것인지.
	 * PC->PlayerState 포인터는 NetGUID 해결을 기다려야 해서 접속 직후 null이다.
	 * UniqueId는 PS 자신의 초기 번치에 실려 오므로 그보다 이르고, 도착 순서에도 의존하지 않는다.
	 */
	UFUNCTION(BlueprintPure, Category = "Match")
	bool IsLocal() const;

	UFUNCTION(BlueprintPure, Category = "Loadout")
	const FDLPlayerLoadout& GetLoadout() const { return Loadout; }

	UFUNCTION(BlueprintPure, Category = "Loadout")
	EDLHero GetHeroType() const { return Loadout.HeroType; }

	// 서버 전용
	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void SetHeroType(EDLHero NewHeroType);

	UFUNCTION(BlueprintCallable, Category = "Loadout")
	void SetPlayerNickname(const FString& NewName);

	// 체력이 바뀔 때 발화. 팀원 UI는 바인딩 직후 현재값도 한 번 읽어야 한다.
	UPROPERTY(BlueprintAssignable, Category = "Vitals")
	FOnPlayerVitalsChanged OnVitalsChanged;

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHealth() const { return Vitals.Health; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetMaxHealth() const { return Vitals.MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHealthCapMultiplier() const { return Vitals.HealthCapMultiplier; }

	// 프로그레스 바에 그대로 꽂을 0~1 값
	UFUNCTION(BlueprintPure, Category = "Vitals")
	float GetHealthPercent() const;

	// 서버 전용. 캐릭터가 자기 체력이 바뀔 때 밀어 넣는다.
	void SetVitals(float NewHealth, float NewMaxHealth, float NewHealthCapMultiplier);

	// 골드가 바뀔 때 발화. 바인딩 직후 GetGold()로 현재값도 한 번 읽어야 한다.
	UPROPERTY(BlueprintAssignable, Category = "Gold")
	FOnGoldChanged OnGoldChanged;

	UFUNCTION(BlueprintPure, Category = "Gold")
	int32 GetGold() const { return Gold; }

	// 구매 UI가 버튼 활성화 여부를 판단할 때 쓴다. 클라에서도 안전하게 읽을 수 있다.
	UFUNCTION(BlueprintPure, Category = "Gold")
	bool CanAfford(int32 Cost) const { return Cost >= 0 && Gold >= Cost; }

	// 서버 전용. 몬스터 처치 보상 등 골드 획득 경로가 호출한다. 0 이하는 무시한다.
	UFUNCTION(BlueprintCallable, Category = "Gold")
	void AddGold(int32 Amount);

	// 서버 전용. 잔액이 모자라면 아무것도 차감하지 않고 false를 돌려준다.
	// 결제와 지급을 한 함수에서 처리하지 않으므로, 호출부는 false일 때 물건을 주면 안 된다.
	UFUNCTION(BlueprintCallable, Category = "Gold")
	bool TrySpendGold(int32 Cost);

	/** Server-only generation record. Future pity rules should read this history, not alter generation callers. */
	void RecordGeneratedWeapon(
		EGunRarity Rarity,
		EWeaponCategory WeaponCategory,
		EWeaponGenerationSource Source);

	UFUNCTION(BlueprintPure, Category = "Weapon Generation")
	const TArray<FGeneratedWeaponRecord>& GetRecentGeneratedWeaponHistory() const { return GeneratedWeaponHistory; }

	UFUNCTION(BlueprintPure, Category = "Weapon Generation")
	int32 GetGeneratedWeaponCount() const { return TotalGeneratedWeaponCount; }

	UFUNCTION(BlueprintPure, Category = "Weapon Generation|Pity")
	int32 GetShopPullsSinceLegendary() const { return ShopPullsSinceLegendary; }

	UFUNCTION(BlueprintPure, Category = "Weapon Generation|Pity")
	int32 GetShopPullsSinceEpic() const { return ShopPullsSinceEpic; }

	UFUNCTION(BlueprintPure, Category = "Weapon Generation")
	int32 GetGeneratedRarityCount(EGunRarity Rarity) const;

	UFUNCTION(BlueprintPure, Category = "Weapon Generation")
	int32 GetGeneratedCategoryCount(EWeaponCategory WeaponCategory) const;

private:
	UPROPERTY(ReplicatedUsing = OnRep_RunStatistics)
	FRunStatistics RunStatistics;

	UFUNCTION()
	void OnRep_RunStatistics();
	bool CanRecordRunStatistics() const;

	UPROPERTY(ReplicatedUsing = OnRep_RoomJoinOrder)
	int32 RoomJoinOrder = INDEX_NONE;

	UFUNCTION()
	void OnRep_RoomJoinOrder();

	// 로비에서 게임맵으로 유지되는 데이터
	UPROPERTY(ReplicatedUsing = OnRep_Loadout)
	FDLPlayerLoadout Loadout;

	UFUNCTION()
	void OnRep_Loadout();

	// 로컬 판정이 가능해지는 시점. 컨트롤러의 UIInitialize를 쏜다.
	virtual void OnRep_UniqueId() override;

	// 구조체 하나로 묶어서 복제한다. 필드를 따로 두면 OnRep이 두 번 돈다.
	UPROPERTY(ReplicatedUsing = OnRep_Vitals)
	FDLPlayerVitals Vitals;

	UFUNCTION()
	void OnRep_Vitals();

	// 심리스 트래블로 PS가 새로 만들어져도 CopyProperties가 값을 넘긴다.
	UPROPERTY(ReplicatedUsing = OnRep_Gold)
	int32 Gold = 100;

	UFUNCTION()
	void OnRep_Gold();

	// 서버/클라 공통으로 델리게이트를 쏘는 지점. LastBroadcastGold 갱신도 여기서 한다.
	void NotifyGoldChanged(int32 Delta);

	// 델타 계산용 직전 값. 복제되지 않는 로컬 캐시다.
	int32 LastBroadcastGold = 100;

	// 첫 복제는 "획득"이 아니라 초기 동기화다. 델타를 0으로 눌러 가짜 획득 연출을 막는다.
	bool bGoldReplicatedOnce = false;

	// GameState에 변경 신호를 넘긴다. 서버는 OnRep이 돌지 않아 세터에서 직접 호출
	void NotifyLoadoutChanged();

	/** Owner-only bounded recent history; cumulative counts and pity state are stored separately. */
	UPROPERTY(Replicated)
	TArray<FGeneratedWeaponRecord> GeneratedWeaponHistory;

	UPROPERTY(Replicated)
	int32 TotalGeneratedWeaponCount = 0;

	UPROPERTY(Replicated)
	FGeneratedWeaponRarityCounts GeneratedRarityCounts;

	UPROPERTY(Replicated)
	FGeneratedWeaponCategoryCounts GeneratedCategoryCounts;

	UPROPERTY(Replicated)
	int32 ShopPullsSinceLegendary = 0;

	UPROPERTY(Replicated)
	int32 ShopPullsSinceEpic = 0;

	static constexpr int32 MaxRecentGeneratedWeaponRecords = 100;
};
