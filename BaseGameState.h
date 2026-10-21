#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "PCGMap/MapBuild/SectorBuild/Event/EventManager.h"
#include "PCGMap/MapBuild/MapStruct.h"
#include "GameCore/HeroType/DLHeroTypes.h"
#include "GameCore/Match/RouteVoteState.h"

#include "BaseGameState.generated.h"

class UEnemySplitHealthState;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomPlayersChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnReturnReadyChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRouteVotesChanged, FIntPoint, Node, int32, Count);

USTRUCT()
struct FPartyReturnReadyState
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<APlayerState>> ReadyPlayers;

	UPROPERTY()
	int32 PlayerCount = 0;

	UPROPERTY()
	bool bTravelStarted = false;
};

// 늦게 접속해도 최신 음악 상태를 받도록 단발 알림 대신 상태를 복제한다.
USTRUCT()
struct FReplicatedMusicState
{
	GENERATED_BODY()


	UPROPERTY()
	bool bHasState = false;

	UPROPERTY()
	float ParameterValue = 0.0f;

	UPROPERTY()
	uint32 Revision = 0;
};

// 지금 들어가 있는 섹터. 복제되므로 늦게 들어온 클라도 OnRep 한 번으로 따라온다
USTRUCT()
struct FSectorStreamState
{
	GENERATED_BODY()


	UPROPERTY()
	TSoftObjectPtr<UWorld> Level;

	UPROPERTY()
	FIntPoint Pos = FIntPoint(0, -1);

	// 같은 섹터를 다시 열어도 OnRep 이 뜨게 하는 값
	UPROPERTY()
	int32 Serial = 0;

	// 재도전 횟수. 0 이 아니면 스트리밍 인스턴스 이름에 붙는다 —
	// 내려가는 중인 옛 인스턴스와 이름이 겹치면 새로 못 올린다
	UPROPERTY()
	int32 Retry = 0;
};

UCLASS()
class ABaseGameState : public AGameStateBase
{
	GENERATED_BODY()


public:
	ABaseGameState();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool StartRouteVote();
	void SubmitRouteVote(APlayerController* Requester, int32 Round, FIntPoint Node);
	void CancelRouteVote();
	void HandleRoutePlayerJoined(APlayerController* Player);

	const FRouteVoteState& GetRouteVote() const { return RouteVote; }

	UPROPERTY(BlueprintAssignable, Category = "Route Vote")
	FOnRouteVotesChanged OnRouteVotesChanged;

	UPROPERTY(EditDefaultsOnly, Category = "Route Vote", meta = (ClampMin = "1.0"))
	float RouteVoteDuration = 45.f;

	UPROPERTY(EditDefaultsOnly, Category = "Route Vote|Sound")
	TObjectPtr<USoundBase> RouteDecisionSound;

	// 서버에서 상태를 설정하고 각 머신의 음악 Director에 전달한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Audio|Music")
	void SetMusicState(float Value);

	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnRoomPlayersChanged OnRoomPlayersChanged;

	virtual void AddPlayerState(APlayerState* PlayerState) override;

	virtual void RemovePlayerState(APlayerState* PlayerState) override;

	// 로컬 PlayerState가 아직 안 왔으면 None
	EDLHero GetLocalHero() const;

	//EventManager는 클라이언트마다 따로 있으니 화면 텍스트는 여기를 거쳐 뿌린다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastScreenText(const FText& Text, EScreenIntensity Intensity, float Duration);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastShowClear(const FText& Text, float Duration);

	//남은 시간(초). 0 이하면 표시를 지운다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastTimeLimit(float Remaining);

	//게임오버도 같은 이유로 여기를 거친다. 서버에서만 부르면 클라에 창이 안 뜬다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastGameOver();

	// 서버 전용. 클리어 상태로 바꾸고 모든 머신에 알린다. 두 번째부터는 무시한다
	void SetGameCleared();

	void StartRunStatistics();

	void FinalizeRunStatistics();

	UFUNCTION(BlueprintPure, Category = "Run Statistics")

	float GetClearDuration() const
	{
		return ClearDurationSeconds;
	}

	// 클리어한 뒤에 늦게 온 게임오버 등을 거를 때 쓴다
	UFUNCTION(BlueprintPure, Category = "Match")

	bool IsGameCleared() const
	{
		return bGameCleared;
	}

	// 클리어도 게임오버와 같은 이유로 여기를 거친다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastGameClear();

	// 서버 전용. 클리어 후 포탈 사용 시 전원에게 결과 표시를 한 번만 알린다.
	void ShowClearResults();

	// 요청자는 소유 PlayerController RPC에서만 전달한다.
	void RequestReturnToRoom(APlayerController* Requester);

	void HandlePlayerLeaving(APlayerState* LeavingPlayer);

	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnReturnReadyChanged OnReturnReadyChanged;

	int32 GetReturnReadyCount() const
	{
		return ReturnReadyState.ReadyPlayers.Num();
	}

	int32 GetReturnPlayerCount() const
	{
		return ReturnReadyState.PlayerCount;
	}

	bool IsPlayerReturnReady(const APlayerState* InPlayer) const;

	bool IsReturnTravelStarted() const
	{
		return ReturnReadyState.bTravelStarted;
	}

	UFUNCTION(NetMulticast, Reliable)
	void MulticastShowClearResults();

	/** 보스 전투 HUD도 GameClear와 같은 GameState -> EventManager 경로로 전원에게 알린다. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastBossBattleHUD(class ABossMonsterCharacter* Boss, bool bShow);

	void MulticastEncounterBattleHUD(class ACommonEnemyCharacter* Enemy, bool bShow);

	void RefreshSharedEncounterHealth(UEnemySplitHealthState* State);

	UPROPERTY(Replicated)
	bool bSharedEncounterHealth = false;

	UPROPERTY(Replicated)
	float SharedEncounterHealth = 0.f;

	UPROPERTY(Replicated)
	float SharedEncounterMaxHealth = 0.f;

	UPROPERTY(Replicated)
	FText SharedEncounterTraits;

	// 서버 전용. 이번에 들어갈 섹터를 전원에게 알린다
	void SetSector(TSoftObjectPtr<UWorld> Level, FIntPoint Pos, int32 Retry = 0);

	// 조립 결과는 그대로 두고 다시 세운다. 같은 방이 같은 모양으로 다시 선다
	void ResendSectorPlacements();

	// 서버 전용. 조립한 방을 전원에게 알린다. 액터는 각 머신이 직접 스폰한다
	void SetSectorPlacements(const TArray<FSectorPlacement>& Placements);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_RouteVote)
	FRouteVoteState RouteVote;

	FTimerHandle RouteVoteTimer;

	UFUNCTION()
	void OnRep_RouteVote(const FRouteVoteState& Previous);

	void ResolveRouteVote(bool bTimeExpired = false);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayRouteDecisionSound();
	void UpdateRouteVoters();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastEncounterHUDState(class ACommonEnemyCharacter* Enemy, bool bShow, bool bShared, float Health,
	                                float MaxHealth, const FText& Traits);

	UPROPERTY(Transient)
	TObjectPtr<UEnemySplitHealthState> DisplayedSplitHealth;

	UPROPERTY(ReplicatedUsing = OnRep_MusicState)
	FReplicatedMusicState MusicState;

	UFUNCTION()
	void OnRep_MusicState();

	void ApplyMusicState();

	UPROPERTY(ReplicatedUsing = OnRep_ReturnReadyState)
	FPartyReturnReadyState ReturnReadyState;

	TSet<TWeakObjectPtr<APlayerState>> LeavingPlayers;

	UFUNCTION()
	void OnRep_ReturnReadyState();

	void UpdateReturnReadiness();

	TArray<APlayerState*> GetConnectedPlayers() const;

	UPROPERTY(Replicated)
	float ClearDurationSeconds = 0.0f;

	// 서버의 중복 요청 방지용. 새 월드에서는 다시 false로 시작한다.
	bool bClearResultsShown = false;

	// 클리어한 뒤에 붙은 클라도 상태를 알 수 있게 복제한다
	UPROPERTY(Replicated)
	bool bGameCleared = false;

	UPROPERTY(ReplicatedUsing = OnRep_Sector)
	FSectorStreamState Sector;

	UFUNCTION()
	void OnRep_Sector();

	UPROPERTY(Replicated)
	TArray<FSectorPlacement> SectorPlacements;

	// 배열에 직접 OnRep 을 걸면 조립 실패(빈 배열)가 클라에 안 알려져 검은 화면이 안 걷힌다
	UPROPERTY(ReplicatedUsing = OnRep_SectorPlacements)
	uint8 PlacementSerial = 0;

	UFUNCTION()
	void OnRep_SectorPlacements();
};
