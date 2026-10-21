#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameCore/UI/UIStruct.h"
#include "PCGMap/MapBuild/MapStruct.h"

#include "BasePlayerController.generated.h"

class URouteWidget;
class UBaseWidgetLayer;
class UDefaultWidgetLayer;
class UScreenVisualFeedbackComponent;
class USmartTrackingVisualComponent;
class UUserWidget;
class UClearResultsWidget;
class ABasePlayerState;
class ABaseGameState;
class AJumpMapLayoutController;

// 로컬 플레이어를 식별할 수 있게 된 시점. UI는 여기서 초기화한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUIInitialize);

UCLASS()
class ABasePlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ABasePlayerController();
	virtual void FlushPressedKeys() override;

	UFUNCTION(Client, Reliable)
	void Client_OpenRouteMap(int32 Seed, const FWorldGenSheet& Sheet, FIntPoint CurPos, const TArray<FIntPoint>& Visited,
		int32 VoteRound, double VoteEndServerTime);

	UFUNCTION(Server, Reliable)
	void Server_VoteRouteNode(int32 VoteRound, FIntPoint Pos);

	// 현재 레벨을 다시 연다. 누가 눌러도 서버가 전원을 함께 옮긴다
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void UIRestartLevel();

	UFUNCTION(Server, Reliable)
	void Server_RestartLevel();

	// 지금 노드를 그대로 다시 연다. 루트 진행은 그대로 둔다
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void UIRetryLevel();

	UFUNCTION(Server, Reliable)
	void Server_RetryLevel();

	UFUNCTION(Server, Reliable)
	void Server_RequestReturnToRoom();
	UFUNCTION(Client, Reliable)
	void Client_PreparePartyReturn();

	/*
	 * 레벨 전환 시작. 트래블 직전에 서버가 전원에게 보낸다.
	 * 화면을 어둡게 깔고 로딩창을 띄운다. 내려주는 쪽은 없다 — 월드가 바뀌면서 같이 사라진다
	 */
	UFUNCTION(Client, Reliable)
	void Client_BeginLevelTransition(float FadeTime);

	// 도착. 스트리밍은 월드가 안 바뀌므로 깔아둔 검은 화면을 직접 걷는다
	void EndLevelTransition();

	// 배치 처리는 점프맵 컨트롤러가 맡고, 소유된 PC는 준비 완료 통신만 전달한다.
	UFUNCTION(Server, Reliable)
	void Server_ReportJumpMapReady(AJumpMapLayoutController* LayoutController, bool bSuccess);

	UFUNCTION(Client, Reliable)
	void Client_FinishJumpMapLoading(AJumpMapLayoutController* LayoutController);

	// 베이스 레이어 스택을 통해 위젯을 띄운다
	UFUNCTION(BlueprintCallable, Category = "UI")
	UDefaultWidgetLayer* ShowWidget(ELayerName LayerName);

	// 스택 맨 위 창을 닫는다. ESC와 같은 경로
	UFUNCTION(BlueprintCallable, Category = "UI")
	void CloseWidget();

	// 레이어 스택 밖에서 떠 있는 UI(루트맵). 스택이 비어도 게임 입력으로 돌아가면 안 된다.
	bool HasUIOutsideLayers() const;

	/*
	 * PlayerState의 UniqueId가 복제로 도착하면 발화한다. ABasePlayerState::OnRep_UniqueId가 쏜다.
	 * 접속 직후에는 로컬 판정이 불가능하므로, 자기 자신을 구분해야 하는 UI는 여기 바인딩한다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "UI")
	FOnUIInitialize UIInitialize;

	/*
	 * 컷신 동안 게임 HUD 를 통째로 접고 스킵 안내만 띄운다.
	 * 베이스 레이어를 지우지 않고 숨기기만 한다 — 다시 만들면 떠 있던 값이 전부 날아간다
	 */
	void SetCinematicUI(bool bOn);

	// ESC. 컷신 중이면 스킵 투표, 아니면 창 닫기
	void HandleEscape();

	// 컷신 스킵. 입력은 각 클라가 받고 판정은 서버가 한다
	UFUNCTION(BlueprintCallable, Category = "Cinematic")
	void RequestCinematicSkip();

	UFUNCTION(Server, Reliable)
	void Server_RequestCinematicSkip();

	// 컷신이 끝난 뒤 카메라가 시퀀스 카메라에서 이 플레이어의 시점으로 넘어오는 블렌드를 마쳤다. 전원이 마치면 서버가 전투를 연다
	UFUNCTION(Server, Reliable)
	void Server_NotifyCinematicReturnReady();


protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_PlayerState() override;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Clear Results")
	TSubclassOf<UClearResultsWidget> ClearResultsWidgetClass;

	// 통계 공급과 서버 복귀 준비 요청을 BP에서 연결할 지점.
	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Clear Results")
	void OnClearResultsOpened(UClearResultsWidget* Widget);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI|Clear Results")
	void OnClearReturnRequested();

	// ESC 처리
	virtual void SetupInputComponent() override;

	// 씬마다 다른 베이스 레이어를 여기서 지정한다. (로비용 / 게임플레이용)
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UBaseWidgetLayer> BaseLayerClass;

	// 컷신 중에만 뜨는 스킵 안내. 표 수는 이 위젯이 EventManager 에 직접 바인딩해 받는다
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> CinematicPromptClass;

	// 레벨에 도착해서 검은 화면이 걷히는 시간. 나가는 페이드와 짝이지만 값은 따로 잡는다
	UPROPERTY(EditDefaultsOnly, Category = "UI", meta = (ClampMin = "0.0"))
	float LevelFadeInTime = 0.6f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UScreenVisualFeedbackComponent> ScreenVisualFeedbackComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USmartTrackingVisualComponent> SmartTrackingVisualComponent;

private:
	UPROPERTY()
	TObjectPtr<UClearResultsWidget> ClearResultsWidget;

	UFUNCTION()
	void HandleShowClearResults();
	UFUNCTION()
	void HandleClearReturnRequested();
	void RemoveClearResults();
	void BindClearStatistics();
	UFUNCTION()
	void RefreshClearResultsData();
	UFUNCTION()
	void RefreshReturnReadyState();
	void BindReturnReadyState();
	TWeakObjectPtr<ABaseGameState> ReturnGameState;
	TWeakObjectPtr<ABasePlayerState> StatisticsPlayerState;

	UPROPERTY()
	TObjectPtr<UBaseWidgetLayer> BaseLayer;

	UPROPERTY()
	TObjectPtr<UUserWidget> CinematicPrompt;

	//루트 관련 로직들 GamePlayerController 따로 만들어서 이관예정
	UPROPERTY()
	TObjectPtr<URouteWidget> RouteWidget;

	void EnsureWidget();

	// 끊겨서 돌아온 것이면 사유 알림을 띄운다. 베이스 레이어가 만들어진 뒤에 부른다.
	void ShowPendingAlert();
};
