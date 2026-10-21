#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "../SectorEvent.h"
#include "PlayCinematicEvent.generated.h"

class ACameraActor;
class ALevelSequenceActor;
class APawn;
class APlayerController;
struct FMinimalViewInfo;
class ULevelSequence;
class ULevelSequencePlayer;

/*
 * 전원에게 같은 컷신을 재생한다. 재생 중에는 조작을 막고, 끝나거나 스킵되면 PostEvents 를 터뜨린다.
 * 끝나는 길은 자연 종료와 스킵 둘인데 둘 다 FinishOnServer 로 모인다 —
 * 갈래를 둘로 두면 스킵했을 때 조작이 안 풀린 채 남는다
 */
UCLASS()
class APlayCinematicEvent : public AScenePlacedEvent
{
	GENERATED_BODY()

public:
	APlayCinematicEvent();

	virtual void Execute(const UObject* WorldContext = nullptr) override;

	// 지금 도는 컷신. 스킵 요청이 플레이어 컨트롤러를 거쳐 여기로 온다
	static APlayCinematicEvent* GetPlaying();

	// 서버에서만 불린다. 표를 다 모았으면 컷신을 끝낸다
	void RequestSkip(APlayerController* Voter);

	// 서버에서만 불린다. 그 플레이어의 카메라가 시퀀스 카메라에서 자기 자리로 돌아오는 블렌드를 마쳤다.
	// 접속한 전원이 마치면 PostEvents(보스 전투 시작 등)를 터뜨린다
	void NotifyReturnReady(APlayerController* Controller);

	UPROPERTY(EditInstanceOnly, Category = "Cinematic")
	TObjectPtr<ULevelSequence> Sequence;

	// 컷신 동안 플레이어 캐릭터를 숨긴다. 시퀀스가 제 카메라로 찍을 때 켠다
	UPROPERTY(EditInstanceOnly, Category = "Cinematic")
	bool bHidePlayer = true;

	/*
	 * 플레이어 대신 걷는 대역. [0]~[3] 순서가 슬롯 번호다. 비우면 대역 없이 기존처럼 재생한다.
	 * 채우면 실제 플레이어는 전원 숨고, PlayerId 순으로 앞 슬롯부터 대역이 그 플레이어의 겉모습을 입는다.
	 * 끝나면 각 플레이어를 자기 대역이 멈춘 자리로 옮긴다
	 */
	UPROPERTY(EditInstanceOnly, Category = "Cinematic|Stand-In")
	TArray<TObjectPtr<AActor>> StandInSlots;

	/*
	 * 컷신이 끝난 뒤 각자의 카메라가 시퀀스 카메라에서 자기 캐릭터의 시점으로 넘어오는 시간(초).
	 * 전원이 넘어오길 기다렸다가 PostEvents 를 터뜨린다. 0 이하이거나 대역을 안 쓰면 바로 끝낸다
	 */
	UPROPERTY(EditInstanceOnly, Category = "Cinematic|Return", meta = (ClampMin = "0.0"))
	float ReturnBlendTime = 1.5f;

	// 블렌드 시간이 지난 뒤에도 서버가 더 기다려 주는 시간(초). 응답 없는 플레이어 때문에 전투가 영영 안 열리는 걸 막는다
	UPROPERTY(EditInstanceOnly, Category = "Cinematic|Return", meta = (ClampMin = "0.0"))
	float ReturnReadyTimeout = 3.0f;

	/*
	 * 서버가 폰을 슬롯 자리로 옮기는 것은 복제로 클라이언트에 늦게 도착한다. 카메라 블렌드는 그 자리에 선 폰을 향해야 하므로,
	 * 모든 폰이 자기 슬롯 예상 위치에서 이 거리(cm) 안에 들어온 뒤에 대역을 치우고 블렌드를 시작한다
	 */
	UPROPERTY(EditInstanceOnly, Category = "Cinematic|Return", meta = (ClampMin = "0.0"))
	float ReturnArrivalTolerance = 50.0f;

	// 폰이 끝내 도착하지 않을 때를 위한 최대 대기(초). 지나면 그대로 블렌드를 시작한다
	UPROPERTY(EditInstanceOnly, Category = "Cinematic|Return", meta = (ClampMin = "0.0"))
	float ReturnArrivalMaxWait = 1.0f;

private:
	bool UsesStandIns() const { return !StandInSlots.IsEmpty(); }

	// 슬롯 순서대로 폰을 돌려준다. 모든 머신이 같은 기준(PlayerId)으로 정렬해야 누구 화면이든 배치가 같다
	TArray<APawn*> GetPawnsInSlotOrder() const;

	// 대역에 겉모습을 입히고 실제 플레이어를 숨긴다. 각 머신이 제 화면에서 한다
	void BeginStandIns();

	// 대역을 치우고 플레이어를 다시 보인다. 서버는 플레이어를 대역 자리로 옮긴다
	void EndStandIns();

	// EndStandIns 의 앞 절반. 서버가 폰을 대역 자리로 옮기고, 각 머신이 슬롯별 예상 위치를 기록하고 로컬 시점을 맞춘다.
	// 대역은 그대로 보이고 폰은 숨은 채 둔다 — 폰이 도착하기 전에 보이면 옛 자리에서 튄다
	void PlaceAtStandIns();

	// EndStandIns 의 뒤 절반. 대역을 치우고 실제 플레이어를 다시 보인다
	void SwapToPlayers();

	// 모든 슬롯의 폰이 기록해 둔 예상 위치 근처에 도착했는가
	bool HavePlayersArrived() const;

	// 도착을 기다리는 타이머 콜백. 전원이 도착했거나 최대 대기를 넘기면 블렌드를 시작한다
	void PollPlayersArrived();

	// 대역을 치우고 자기 폰으로 카메라 블렌드를 시작한다
	void StartCameraBlend();

	// 실제 플레이어와 들고 있는 무기의 숨김을 켜고 끈다. 켤 때 원래 상태를 기억했다가 끌 때 되돌린다
	void SetPlayersHidden(bool bHide);

	// 숨기기 전 상태. 원래 숨어 있던 액터를 컷신이 끝났다고 드러내면 안 된다
	TMap<TWeakObjectPtr<AActor>, bool> HiddenBeforeCinematic;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlay();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStop();

	// 카메라 복귀 블렌드가 전원에게서 끝났다. 조작을 돌려주고 컷신을 정리한다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReturnFinished();

	// 대역을 쓰고 블렌드 시간이 있어야 시퀀스 카메라에서 제 시점으로 블렌드한다
	bool ShouldBlendReturn() const { return UsesStandIns() && ReturnBlendTime > 0.0f; }

	// 이 머신의 로컬 컨트롤러. 전용 서버에는 없다
	APlayerController* GetLocalController() const;

	// 시퀀스가 멈추면 시점이 원래대로 튀므로, 멈추기 직전의 화면을 임시 카메라에 옮겨 놓고 거기서 자기 폰으로 블렌드한다
	void BeginCameraReturn(APlayerController* Controller, const FMinimalViewInfo& StartView);

	// 블렌드가 끝났다. 임시 카메라를 치우고 서버에 알린다
	void FinishCameraReturn();

	// 서버에서 한 번만 통과한다. 전원이 마쳤거나 기다리다 지쳤을 때 PostEvents 를 터뜨린다
	void CompleteReturn();

	// 이 머신의 조작·HUD·컷신 상태를 원래대로 돌린다
	void EndCinematicLocal();

	// 표 수를 전원 화면에 알린다. Total 0 이면 표시를 지운다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastSkipVotes(int32 Voted, int32 Total);

	// 접속한 플레이어 수. 서버에서만 센다
	int32 CountPlayers() const;

	// 이 머신의 HUD 에 표 수를 넘긴다
	void SetCinematicSkipVotesOnHud(int32 Voted, int32 Total) const;

	UFUNCTION()
	void OnSequenceFinished();

	// 끝나는 유일한 길. 서버 전용이고 한 번만 통과한다
	void FinishOnServer();

	// 이 머신의 로컬 컨트롤러에만 건다
	void SetCinematicInput(bool bOn);

	UPROPERTY()
	TObjectPtr<ALevelSequenceActor> SequenceActor;

	UPROPERTY()
	TObjectPtr<ULevelSequencePlayer> SequencePlayer;

	TSet<TWeakObjectPtr<APlayerController>> SkipVotes;

	// 시퀀스 카메라 자리에서 블렌드를 출발시키는 임시 카메라. 이 머신에서만 존재한다
	UPROPERTY()
	TObjectPtr<ACameraActor> ReturnCamera;

	// 복귀 블렌드를 마쳤다고 알려 온 플레이어. 서버만 채운다
	TSet<TWeakObjectPtr<APlayerController>> ReturnReady;

	FTimerHandle ReturnBlendTimer;
	FTimerHandle ReturnTimeoutTimer;
	FTimerHandle ReturnArrivalTimer;

	// 슬롯 번호별 대역의 최종 자리. 도착 판정의 기준이다. 시퀀스가 멈추기 전에 읽어 둔다
	TMap<int32, FTransform> ReturnSlotTransforms;

	double ReturnArrivalStartTime = 0.0;

	bool bFinished = false;

	// PostEvents 를 이미 터뜨렸다. 전원 보고와 타임아웃이 겹쳐도 한 번만 통과시킨다
	bool bReturnCompleted = false;
};


