#include "LobbyPlayerController.h"

#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"
#include "GameCore/BaseGameState.h"
#include "GameCore/BasePlayerState.h"
#include "GameCore/Match/RunStatisticsSubsystem.h"
#include "GameCore/Match/PartyRoomSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameCore/Match/RoomPlayerEntryWidget.h"
#include "GameCore/Match/SessionEntryData.h"
#include "PCGMap/MapBuild/MapStatic.h"

namespace
{
	// 방 식별용 세션 설정 키
	const FName GameKeyName(TEXT("DLGAMEKEY"));
}

void ALobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		UPartyRoomSubsystem* Room = GetGameInstance()->GetSubsystem<UPartyRoomSubsystem>();
		const bool bReturnedToRoom = Room->IsReturning();
		// 권한이 없으면 방에 참가해 들어온 것이다
		if (!HasAuthority() || bReturnedToRoom)
		{
			ShowWidget(ELayerName::MatchRoom);
		}
		if (bReturnedToRoom)
		{
			Room->FinishReturn();
			if (HasAuthority())
			{
				RestoreRoomSession();
			}
		}

		// 대기실을 띄운 뒤에 바인딩한다
		BindRoomUpdates();
	}
}

void ALobbyPlayerController::BindRoomUpdates()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (ABaseGameState* GS = World->GetGameState<ABaseGameState>())
	{
		GS->OnRoomPlayersChanged.AddDynamic(this, &ALobbyPlayerController::OnRoomUpdated_Broadcast);

		// 바인딩 전에 도착한 PlayerState를 반영
		OnRoomUpdated_Broadcast();
		return;
	}

	// 클라는 접속 직후 GameState가 없다. 도착하면 다시 시도
	World->GameStateSetEvent.AddWeakLambda(this, [this](AGameStateBase*) { BindRoomUpdates(); });
}

void ALobbyPlayerController::OnRoomUpdated_Broadcast()
{
	OnRoomUpdated.Broadcast();
}

void ALobbyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearDelegates();

	Super::EndPlay(EndPlayReason);
}

IOnlineSessionPtr ALobbyPlayerController::GetSessionInterface() const
{
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	if (!OSS)
	{
		UE_LOG(LogTemp, Error, TEXT("[Match] OnlineSubsystem 없음. Steam 클라이언트가 켜져 있는지 확인."));
		return nullptr;
	}
	return OSS->GetSessionInterface();
}

void ALobbyPlayerController::CreateGame(const FString& RoomName)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	PendingRoomName = RoomName;

	// 남의 방에 들어갔다 나오면 네임드 세션이 남아 있다. 파괴가 끝나기 전에 만들면 실패한다
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		DestroyThen(EPendingSessionAction::Create);
		return;
	}

	StartCreateSession();
}

// 파괴 완료를 기다렸다가 이어서 할 일을 예약한다. 파괴는 비동기다
void ALobbyPlayerController::DestroyThen(EPendingSessionAction Action)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	PendingAction = Action;

	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &ALobbyPlayerController::OnDestroyComplete));

	Sessions->DestroySession(NAME_GameSession);
}

void ALobbyPlayerController::OnDestroyComplete(FName SessionName, bool bSuccess)
{
	ClearDelegates();

	if (!bSuccess)
	{
		// 실패해도 이어서 진행한다. 만들기가 또 실패하면 OnCreateComplete가 알린다
		UE_LOG(LogTemp, Warning, TEXT("[Match] 이전 세션 정리 실패. 그대로 진행한다."));
	}

	const EPendingSessionAction Action = PendingAction;
	PendingAction = EPendingSessionAction::None;

	switch (Action)
	{
	case EPendingSessionAction::Create:
		StartCreateSession();
		break;

	case EPendingSessionAction::ReturnToLobby:
		ClientTravel(LobbyMapName, TRAVEL_Absolute);
		break;

	default:
		break;
	}
}

void ALobbyPlayerController::StartCreateSession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = false;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true; // 시작할 때 StartGame에서 내린다
	Settings.bUsesPresence = true;
	Settings.bUseLobbiesIfAvailable = true; // Steam 로비로 광고된다
	Settings.Set(SEARCH_KEYWORDS, PendingRoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(GameKeyName, GameKey, EOnlineDataAdvertisementType::ViaOnlineService);

	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &ALobbyPlayerController::OnCreateComplete));

	Sessions->CreateSession(0, NAME_GameSession, Settings);
}

void ALobbyPlayerController::OnCreateComplete(FName SessionName, bool bSuccess)
{
	ClearDelegates();

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("[Match] 방 생성 실패."));
		OnRoomCreated.Broadcast(false);
		return;
	}

	// 현재 월드에서 넷 드라이버만 열어 리슨 서버가 된다
	FURL URL;
	if (!GetWorld()->Listen(URL))
	{
		UE_LOG(LogTemp, Error, TEXT("[Match] 리슨 서버 시작 실패."));
		OnRoomCreated.Broadcast(false);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Match] 방 생성 완료: %s"), *PendingRoomName);
	OnRoomCreated.Broadcast(true);
}

void ALobbyPlayerController::FindGames()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = false;
	Search->MaxSearchResults = 50;
	Search->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	Search->QuerySettings.Set(GameKeyName, GameKey, EOnlineComparisonOp::Equals);

	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &ALobbyPlayerController::OnFindComplete));

	Sessions->FindSessions(0, Search.ToSharedRef());
}

void ALobbyPlayerController::OnFindComplete(bool bSuccess)
{
	ClearDelegates();

	FoundSessions.Reset();

	if (bSuccess && Search.IsValid())
	{
		for (int32 Index = 0; Index < Search->SearchResults.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Result = Search->SearchResults[Index];

			// 서버측 필터가 무시될 수 있어 결과를 다시 확인
			FString FoundKey;
			Result.Session.SessionSettings.Get(GameKeyName, FoundKey);
			if (FoundKey != GameKey)
			{
				continue;
			}

			USessionEntryData* Entry = NewObject<USessionEntryData>(this);
			Result.Session.SessionSettings.Get(SEARCH_KEYWORDS, Entry->RoomName);
			if (Entry->RoomName.IsEmpty())
			{
				Entry->RoomName = Result.Session.OwningUserName;
			}
			Entry->MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
			Entry->PingMs = Result.PingInMs;
			Entry->SearchResultIndex = Index;

			// 검색 결과에는 인원 수만 온다. 자리 수만큼 빈 항목을 채운다
			const int32 Taken = Entry->MaxPlayers - Result.Session.NumOpenPublicConnections;
			Entry->CurrentPlayers.SetNum(FMath::Max(0, Taken));

			FoundSessions.Add(Entry);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[Match] 방 %d개."), FoundSessions.Num());
	OnSessionListUpdated.Broadcast();
}

bool ALobbyPlayerController::JoinGame(USessionEntryData* Entry)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Entry || !Search.IsValid())
	{
		return false;
	}

	// 검색 결과 갱신으로 인덱스가 어긋날 수 있어 확인한다
	if (!Search->SearchResults.IsValidIndex(Entry->SearchResultIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Match] 만료된 항목이다. 다시 검색할 것."));
		ShowWidget(ELayerName::Alert_SessionLost);
		return false;
	}

	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &ALobbyPlayerController::OnJoinComplete));

	// 여기부터 트래블 전까지 목록을 잠근다
	OnRoomJoinStarted.Broadcast();

	Sessions->JoinSession(0, NAME_GameSession, Search->SearchResults[Entry->SearchResultIndex]);
	return true;
}

void ALobbyPlayerController::OnJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	ClearDelegates();

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		UE_LOG(LogTemp, Error, TEXT("[Match] 참가 실패. Result=%d"), static_cast<int32>(Result));
		OnRoomJoined.Broadcast(false);

		// 자리가 없는 것과 방이 사라진 것만 구분한다. 나머지 실패도 결국 못 들어간 것이라 같이 묶는다
		ShowWidget(Result == EOnJoinSessionCompleteResult::SessionIsFull
			? ELayerName::Alert_SessionFull
			: ELayerName::Alert_SessionLost);
		return;
	}

	IOnlineSessionPtr Sessions = GetSessionInterface();
	FString ConnectString;
	if (!Sessions.IsValid() || !Sessions->GetResolvedConnectString(NAME_GameSession, ConnectString))
	{
		UE_LOG(LogTemp, Error, TEXT("[Match] 접속 주소를 못 얻었다."));
		OnRoomJoined.Broadcast(false);
		ShowWidget(ELayerName::Alert_SessionLost);
		return;
	}

	// SteamSockets에서는 이 주소가 IP가 아니라 SteamID다
	UE_LOG(LogTemp, Warning, TEXT("[Match] 접속: %s"), *ConnectString);
	ClientTravel(ConnectString, TRAVEL_Absolute);
}

void ALobbyPlayerController::SelectHero(EDLHero NewHero)
{
	// 변경은 서버 권한으로만
	Server_SelectHero(NewHero);
}

void ALobbyPlayerController::Server_SelectHero_Implementation(EDLHero NewHero)
{
	if (ABasePlayerState* PS = GetPlayerState<ABasePlayerState>())
	{
		PS->SetHeroType(NewHero);
	}
}

void ALobbyPlayerController::StartGame()
{
	if (!HasAuthority() || bRestoringRoomSession || bStartingGame)
	{
		return;
	}
	bStartingGame = true;
	GetGameInstance()->GetSubsystem<UPartyRoomSubsystem>()->RememberRoom(
		FName(*UGameplayStatics::GetCurrentLevelName(this, true)));

	// 새 런에서는 이전 결과를 버린다. 이름/영웅/입장 순서는 그대로 유지한다.
	GetGameInstance()->GetSubsystem<URunStatisticsSubsystem>()->ResetRun();
	if (ABaseGameState* GS = GetWorld()->GetGameState<ABaseGameState>())
	{
		for (APlayerState* Entry : GS->PlayerArray)
		{
			if (ABasePlayerState* BasePS = Cast<ABasePlayerState>(Entry))
			{
				BasePS->ResetRunStatistics();
			}
		}
	}

	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		// 시작한 방은 목록에서 내리고 로비 입장도 막는다
		if (FOnlineSessionSettings* Settings = Sessions->GetSessionSettings(NAME_GameSession))
		{
			FOnlineSessionSettings Updated = *Settings;
			Updated.bAllowJoinInProgress = false;
			Sessions->UpdateSession(NAME_GameSession, Updated, true);
		}

		Sessions->StartSession(NAME_GameSession);
	}

	// 접속한 전원이 함께 이동한다. 페이드는 SeamlessTravelTo 안에서 처리된다
	if (!StartLevelTransition(this, FName(*GameplayMapName)))
	{
		bStartingGame = false;
	}
}

void ALobbyPlayerController::RestoreRoomSession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		return; // PIE 등 온라인 세션 없이 실행하는 환경도 대기방 이동은 가능하다.
	}
	const EOnlineSessionState::Type State = Sessions->GetSessionState(NAME_GameSession);
	if (State == EOnlineSessionState::InProgress)
	{
		bRestoringRoomSession = true;
		EndRoomSessionHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(
			FOnEndSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnEndRoomSessionComplete));
		if (!Sessions->EndSession(NAME_GameSession))
		{
			OnEndRoomSessionComplete(NAME_GameSession, false);
		}
		return;
	}
	ReopenRoomSession();
}

void ALobbyPlayerController::OnEndRoomSessionComplete(FName SessionName, bool bSuccess)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndRoomSessionHandle);
	}
	EndRoomSessionHandle.Reset();
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("[PartyReturn] 세션 라운드 종료 실패. 세션 상태를 확인해야 합니다."));
	}
	ReopenRoomSession();
}

void ALobbyPlayerController::ReopenRoomSession()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
	{
		if (FOnlineSessionSettings* Settings = Sessions->GetSessionSettings(NAME_GameSession))
		{
			FOnlineSessionSettings Updated = *Settings;
			Updated.bAllowJoinInProgress = true;
			Updated.bShouldAdvertise = true;
			if (!Sessions->UpdateSession(NAME_GameSession, Updated, true))
			{
				UE_LOG(LogTemp, Warning, TEXT("[PartyReturn] 대기방 세션 설정 업데이트 실패."));
			}
		}
	}
	bRestoringRoomSession = false;
}

void ALobbyPlayerController::LeaveRoom()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();

	// 파괴가 끝나기 전에 트래블하면 컨트롤러가 사라져 완료를 확인할 주체가 없어진다
	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		DestroyThen(EPendingSessionAction::ReturnToLobby);
		return;
	}

	// 호스트든 참가자든 로비 맵으로 돌아간다
	ClientTravel(LobbyMapName, TRAVEL_Absolute);
}

TArray<UObject*> ALobbyPlayerController::GetRoomPlayerEntries()
{
	TArray<UObject*> Entries;

	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!GS)
	{
		return Entries;
	}

	TArray<ABasePlayerState*> RoomPlayers;
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (ABasePlayerState* BasePS = Cast<ABasePlayerState>(PS); BasePS && !BasePS->IsInactive())
		{
			RoomPlayers.Add(BasePS);
		}
	}
	RoomPlayers.StableSort([](const ABasePlayerState& A, const ABasePlayerState& B)
	{
		const int32 OrderA = A.GetRoomJoinOrder() == INDEX_NONE ? MAX_int32 : A.GetRoomJoinOrder();
		const int32 OrderB = B.GetRoomJoinOrder() == INDEX_NONE ? MAX_int32 : B.GetRoomJoinOrder();
		return OrderA < OrderB;
	});

	Entries.Reserve(RoomPlayers.Num());
	for (const APlayerState* PS : RoomPlayers)
	{
		const ABasePlayerState* BasePS = Cast<ABasePlayerState>(PS);
		if (!BasePS)
		{
			continue;
		}

		URoomPlayerEntryData* Entry = NewObject<URoomPlayerEntryData>(this);
		Entry->Loadout = BasePS->GetLoadout();
		Entry->bIsLocal = BasePS->IsLocal();

		Entries.Add(Entry);
	}

	return Entries;
}

bool ALobbyPlayerController::IsHost() const
{
	return HasAuthority();
}

void ALobbyPlayerController::ClearDelegates()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndRoomSessionHandle);
}
