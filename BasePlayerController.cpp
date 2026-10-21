#include "BasePlayerController.h"

#include "PCGMap/MapBuild/SectorBuild/Event/PlacedEvent/PlayCinematicEvent.h"
#include "PCGMap/MapBuild/SectorBuild/Event/EventManager.h"

#include "PCGMap/MapBuild/WorldGenSubSystem.h"
#include "Environment/JumpMapLayoutController.h"
#include "PCGMap/MapBuild/UI/RouteWidget.h"
#include "GameCore/UI/BaseWidgetLayer.h"
#include "GameCore/UI/ClearResultsWidget.h"
#include "GameCore/BasePlayerState.h"
#include "GameCore/BaseGameState.h"
#include "GameCore/Character/PlayerCharacter.h"
#include "GameCore/Match/PartyRoomSubsystem.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameCore/Match/MatchAlertSubsystem.h"
#include "GameCore/Combat/CombatFeedbackComponent.h"
#include "GameCore/UI/ScreenVisualFeedbackComponent.h"
#include "GameCore/UI/SmartTrackingVisualComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CanvasPanel.h"
#include "Kismet/GameplayStatics.h"
#include "PCGMap/MapBuild/MapStatic.h"

ABasePlayerController::ABasePlayerController()
{
	CreateDefaultSubobject<UCombatFeedbackComponent>(TEXT("CombatFeedbackComponent"));
	ScreenVisualFeedbackComponent = CreateDefaultSubobject<UScreenVisualFeedbackComponent>(TEXT("ScreenVisualFeedbackComponent"));
	SmartTrackingVisualComponent = CreateDefaultSubobject<USmartTrackingVisualComponent>(TEXT("SmartTrackingVisualComponent"));
}

void ABasePlayerController::FlushPressedKeys()
{
	// 키 해제 이벤트보다 먼저 정리해야 스마트 트래킹이 메뉴에서 사격하지 않는다.
	if (APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(GetPawn()))
	{
		PlayerCharacter->ResetPersistentInput();
	}
	Super::FlushPressedKeys();
}

void ABasePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 맵을 열면 UIOnly 로 바뀐다. 되돌리는 짝이 여기다.
	// 트래블 방식(absolute/relative/seamless)에 기대지 않고 매 레벨 명시적으로 되돌린다.
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;

		// 도착. 나갈 때 깔아둔 검은 화면을 걷는다. 첫 진입이면 그냥 밝아지고 끝난다
		if (PlayerCameraManager && LevelFadeInTime > 0.0f)
		{
			PlayerCameraManager->StartCameraFade(1.0f, 0.0f, LevelFadeInTime, FLinearColor::Black);
		}

		if (BaseLayerClass)
		{
			// 베이스는 씬당 하나. 생성되면서 Game and UI + 자기 자신 포커스로 바꿔 ESC를 받는다.
			BaseLayer = CreateWidget<UBaseWidgetLayer>(this, BaseLayerClass);
			if (BaseLayer)
			{
				BaseLayer->AddToViewport(0);
				BaseLayer->Show();
				BaseLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				UCanvasPanel* FeedbackCanvas = Cast<UCanvasPanel>(
					BaseLayer->GetWidgetFromName(TEXT("MainCanvas")));
				ScreenVisualFeedbackComponent->SetWidgetHost(FeedbackCanvas);
				SmartTrackingVisualComponent->SetWidgetHost(FeedbackCanvas);
			}
		}

		ShowPendingAlert();
		BindClearStatistics();
		if (UEventManager* Events = GetGameInstance()->GetSubsystem<UEventManager>())
		{
			Events->OnShowClearResults.AddUniqueDynamic(this, &ThisClass::HandleShowClearResults);
		}
	}
}

void ABasePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ABaseGameState* GS = ReturnGameState.Get())
	{
		GS->OnReturnReadyChanged.RemoveDynamic(this, &ThisClass::RefreshReturnReadyState);
	}
	ReturnGameState.Reset();
	if (GetWorld())
	{
		GetWorld()->GameStateSetEvent.RemoveAll(this);
	}
	if (ABasePlayerState* BoundPS = StatisticsPlayerState.Get())
	{
		BoundPS->OnRunStatisticsChanged.RemoveDynamic(this, &ThisClass::RefreshClearResultsData);
	}
	StatisticsPlayerState.Reset();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEventManager* Events = GI->GetSubsystem<UEventManager>())
		{
			Events->OnShowClearResults.RemoveDynamic(this, &ThisClass::HandleShowClearResults);
		}
	}
	RemoveClearResults();
	Super::EndPlay(EndPlayReason);
}

void ABasePlayerController::HandleShowClearResults()
{
	if (!IsLocalController() || ClearResultsWidget)
	{
		return;
	}
	if (!ClearResultsWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClearResults] ClearResultsWidgetClass is not configured."));
		return;
	}
	ClearResultsWidget = CreateWidget<UClearResultsWidget>(this, ClearResultsWidgetClass);
	if (!ClearResultsWidget)
	{
		return;
	}
	if (BaseLayer)
	{
		BaseLayer->SetPresentationHidden(true);
	}
	if (RouteWidget)
	{
		RouteWidget->DismissUI();
	}
	ClearResultsWidget->OnReturnRequested.AddUniqueDynamic(this, &ThisClass::HandleClearReturnRequested);
	ClearResultsWidget->AddToViewport(100);
	BindReturnReadyState();
	RefreshReturnReadyState();
	FlushPressedKeys();
	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	if (APawn* ControlledPawn = GetPawn())
	{
		if (UPawnMovementComponent* Movement = ControlledPawn->GetMovementComponent())
		{
			Movement->StopMovementImmediately();
		}
	}
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(ClearResultsWidget->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	BindClearStatistics();
	RefreshClearResultsData();
	OnClearResultsOpened(ClearResultsWidget);
}

void ABasePlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindClearStatistics();
	RefreshClearResultsData();
	RefreshReturnReadyState();
}

void ABasePlayerController::BindClearStatistics()
{
	if (!IsLocalController())
	{
		return;
	}
	ABasePlayerState* CurrentPS = GetPlayerState<ABasePlayerState>();
	if (StatisticsPlayerState.Get() == CurrentPS)
	{
		return;
	}
	if (ABasePlayerState* PreviousPS = StatisticsPlayerState.Get())
	{
		PreviousPS->OnRunStatisticsChanged.RemoveDynamic(this, &ThisClass::RefreshClearResultsData);
	}
	StatisticsPlayerState = CurrentPS;
	if (CurrentPS)
	{
		CurrentPS->OnRunStatisticsChanged.AddUniqueDynamic(this, &ThisClass::RefreshClearResultsData);
	}
}

void ABasePlayerController::RefreshClearResultsData()
{
	const ABasePlayerState* CurrentPS = GetPlayerState<ABasePlayerState>();
	if (!ClearResultsWidget || !CurrentPS || !CurrentPS->GetRunStatistics().bFinalized)
	{
		return;
	}
	const FRunStatistics& Stats = CurrentPS->GetRunStatistics();
	FClearResultsDisplayData Display;
	Display.TotalDamage = Stats.TotalDamage;
	Display.BossDamage = Stats.BossDamage;
	Display.KillCount = Stats.KillCount;
	Display.DamageTaken = Stats.DamageTaken;
	Display.ReviveCount = Stats.ReviveCount;
	Display.ClearDurationSeconds = Stats.ClearDurationSeconds;
	ClearResultsWidget->SetResultsData(Display);
}

void ABasePlayerController::HandleClearReturnRequested()
{
	Server_RequestReturnToRoom();
	OnClearReturnRequested();
}

void ABasePlayerController::Server_RequestReturnToRoom_Implementation()
{
	if (ABaseGameState* GS = GetWorld()->GetGameState<ABaseGameState>())
	{
		GS->RequestReturnToRoom(this);
	}
}

void ABasePlayerController::BindReturnReadyState()
{
	ABaseGameState* GS = GetWorld()->GetGameState<ABaseGameState>();
	if (!GS)
	{
		GetWorld()->GameStateSetEvent.AddWeakLambda(this, [this](AGameStateBase*)
		{
			BindReturnReadyState();
			RefreshReturnReadyState();
		});
		return;
	}
	if (ReturnGameState.Get() != GS)
	{
		if (ABaseGameState* Previous = ReturnGameState.Get())
		{
			Previous->OnReturnReadyChanged.RemoveDynamic(this, &ThisClass::RefreshReturnReadyState);
		}
		ReturnGameState = GS;
		GS->OnReturnReadyChanged.AddUniqueDynamic(this, &ThisClass::RefreshReturnReadyState);
	}
}

void ABasePlayerController::RefreshReturnReadyState()
{
	const ABaseGameState* GS = ReturnGameState.Get();
	if (ClearResultsWidget && GS)
	{
		ClearResultsWidget->SetReturnReadyState(GS->GetReturnReadyCount(), GS->GetReturnPlayerCount(),
			GS->IsReturnTravelStarted() || GS->IsPlayerReturnReady(PlayerState.Get()));
	}
}

void ABasePlayerController::Client_PreparePartyReturn_Implementation()
{
	GetGameInstance()->GetSubsystem<UPartyRoomSubsystem>()->MarkReturning();
	if (UWorldGenSubSystem* WorldGen = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>())
	{
		WorldGen->ResetRunForLobby();
	}
}

void ABasePlayerController::RemoveClearResults()
{
	if (!ClearResultsWidget)
	{
		return;
	}
	ClearResultsWidget->OnReturnRequested.RemoveDynamic(this, &ThisClass::HandleClearReturnRequested);
	ClearResultsWidget->RemoveFromParent();
	ClearResultsWidget = nullptr;
	SetIgnoreMoveInput(false);
	SetIgnoreLookInput(false);
	if (BaseLayer)
	{
		BaseLayer->SetPresentationHidden(false);
	}
}

void ABasePlayerController::ShowPendingAlert()
{
	UGameInstance* GameInstance = GetGameInstance();
	UMatchAlertSubsystem* Alerts = GameInstance ? GameInstance->GetSubsystem<UMatchAlertSubsystem>() : nullptr;
	if (!Alerts)
	{
		return;
	}

	ELayerName Alert;
	if (Alerts->ConsumePendingAlert(Alert))
	{
		ShowWidget(Alert);
	}
}

void ABasePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ABasePlayerController::HandleEscape);
}

UDefaultWidgetLayer* ABasePlayerController::ShowWidget(ELayerName LayerName)
{
	return BaseLayer ? BaseLayer->ShowWidget(LayerName) : nullptr;
}

void ABasePlayerController::CloseWidget()
{
	if (BaseLayer)
	{
		BaseLayer->CloseTop();
	}
}

bool ABasePlayerController::HasUIOutsideLayers() const
{
	return (ClearResultsWidget && ClearResultsWidget->IsVisible()) || (RouteWidget && RouteWidget->IsVisible());
}

void ABasePlayerController::EnsureWidget()
{
	if (RouteWidget)
	{
		return;
	}

	UWorldGenSubSystem* Sys = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>();
	if (!Sys || !Sys->RouteWidgetClass || !Sys->GetRoute())
	{
		UE_LOG(LogTemp, Error, TEXT("[RouteMap] 위젯 클래스 또는 루트 없음. World Gen 설정 확인."));
		return;
	}

	RouteWidget = CreateWidget<URouteWidget>(this, Sys->RouteWidgetClass);
	if (!RouteWidget)
	{
		return;
	}

	RouteWidget->Init(Sys->GetRoute(), Sys->GetRunSeed());
	RouteWidget->AddToViewport();
}

void ABasePlayerController::Client_OpenRouteMap_Implementation(int32 Seed, const FWorldGenSheet& Sheet, FIntPoint CurPos,
	const TArray<FIntPoint>& Visited, int32 VoteRound, double VoteEndServerTime)
{
	UWorldGenSubSystem* Sys = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>();
	if (!Sys)
	{
		return;
	}

	// 클라는 여기서 처음 시드를 받는다. 서버에서는 이미 만들어져 있어 no-op.
	Sys->EnsureRoute(Seed, Sheet);

	EnsureWidget();
	if (!RouteWidget)
	{
		return;
	}

	// 순서 주의. 갈 수 있는 곳을 먼저 켜고, 지나온 곳으로 덮는다
	RouteWidget->ActiveWidget(CurPos);
	RouteWidget->ApplyVisited(Visited);
	RouteWidget->VisibleUI(CurPos, VoteRound, VoteEndServerTime);

	SetInputMode(FInputModeUIOnly());
	bShowMouseCursor = true;
}

void ABasePlayerController::Client_BeginLevelTransition_Implementation(float FadeTime)
{
	RemoveClearResults();
	// 게임오버 같은 창을 닫을 기회를 UI 에 준다
	if (UEventManager* Events = GetGameInstance()->GetSubsystem<UEventManager>())
	{
		Events->BroadcastLevelTransitionBegin();
	}

	// 씬만 덮는다. 로딩창은 이 위에 그려진다
	if (PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraFade(
			0.0f, 1.0f, FadeTime, FLinearColor::Black, false, /*bHoldWhenFinished=*/true);
	}

	// 루트맵은 레이어 스택 밖이라 CloseWidget 으로 안 닫힌다.
	if (RouteWidget)
	{
		RouteWidget->CloseUI();
	}

	ShowWidget(ELayerName::Loading);
}

void ABasePlayerController::EndLevelTransition()
{
	if (PlayerCameraManager && LevelFadeInTime > 0.0f)
	{
		PlayerCameraManager->StartCameraFade(1.0f, 0.0f, LevelFadeInTime, FLinearColor::Black);
	}

	CloseWidget(); // 로딩창

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}

void ABasePlayerController::Server_ReportJumpMapReady_Implementation(AJumpMapLayoutController* LayoutController, bool bSuccess)
{
	if (UWorldGenSubSystem* Sys = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>())
	{
		Sys->ReportJumpMapReady(this, LayoutController, bSuccess);
	}
}

void ABasePlayerController::Client_FinishJumpMapLoading_Implementation(AJumpMapLayoutController* LayoutController)
{
	if (UWorldGenSubSystem* Sys = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>())
	{
		Sys->CompleteJumpMapTransition(LayoutController);
	}
}

void ABasePlayerController::Server_VoteRouteNode_Implementation(int32 VoteRound, FIntPoint Pos)
{
	if (ABaseGameState* GS = GetWorld()->GetGameState<ABaseGameState>())
	{
		GS->SubmitRouteVote(this, VoteRound, Pos);
	}
}

void ABasePlayerController::UIRestartLevel()
{
	Server_RestartLevel();
}

void ABasePlayerController::SetCinematicUI(bool bOn)
{
	if (BaseLayer)
	{
		BaseLayer->SetVisibility(bOn ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	if (!bOn)
	{
		if (CinematicPrompt)
		{
			CinematicPrompt->RemoveFromParent();
			CinematicPrompt = nullptr;
		}
		return;
	}

	// 안 꽂았으면 안내 없이 컷신만 돈다. 여기서 죽일 일은 아니다
	if (!CinematicPromptClass || CinematicPrompt)
	{
		return;
	}

	CinematicPrompt = CreateWidget<UUserWidget>(this, CinematicPromptClass);
	if (CinematicPrompt)
	{
		CinematicPrompt->AddToViewport(1);
	}
}

void ABasePlayerController::HandleEscape()
{
	if (ClearResultsWidget)
	{
		return;
	}
	// 컷신이 도는 동안 ESC 는 스킵 투표다. 컷신 중에는 창이 떠 있을 일이 없다
	if (APlayCinematicEvent::GetPlaying())
	{
		RequestCinematicSkip();
		return;
	}

	CloseWidget();
}

void ABasePlayerController::RequestCinematicSkip()
{
	Server_RequestCinematicSkip();
}

void ABasePlayerController::Server_RequestCinematicSkip_Implementation()
{
	if (APlayCinematicEvent* Cinematic = APlayCinematicEvent::GetPlaying())
	{
		Cinematic->RequestSkip(this);
	}
}

void ABasePlayerController::Server_NotifyCinematicReturnReady_Implementation()
{
	if (APlayCinematicEvent* Cinematic = APlayCinematicEvent::GetPlaying())
	{
		Cinematic->NotifyReturnReady(this);
	}
}

void ABasePlayerController::UIRetryLevel()
{
	Server_RetryLevel();
}

void ABasePlayerController::Server_RetryLevel_Implementation()
{
	if (UWorldGenSubSystem* Sys = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>())
	{
		Sys->RetryCurrentSector();
	}
}

void ABasePlayerController::Server_RestartLevel_Implementation()
{
	// 리스타트는 런 포기다. 로비로 돌아간 뒤 지도가 옛 진행을 가리키면 안 된다
	if (UWorldGenSubSystem* Sys = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>())
	{
		Sys->AbandonRun();
	}

	// 페이드와 로딩창은 StartLevelTransition 안에서 처리된다
	StartLevelTransition(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}
