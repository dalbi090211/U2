#include "BaseGameState.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#include "GameCore/Combat/EnemySplitComponent.h"
#include "GameCore/Character/CommonEnemyCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameCore/Audio/MusicDirectorSubsystem.h"
#include "GameCore/BasePlayerState.h"
#include "GameCore/BaseGameMode.h"
#include "GameCore/BasePlayerController.h"
#include "GameFramework/PlayerController.h"
#include "GameCore/Match/RunStatisticsSubsystem.h"
#include "GameCore/Character/BossMonsterCharacter.h"
#include "PCGMap/MapBuild/WorldGenSubSystem.h"
#include "PCGMap/MapBuild/SectorBuild/SectorSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectMusicState, Log, All);

ABaseGameState::ABaseGameState()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> Sound(
		TEXT("/Game/TPSDevelop/Sound/UI/zapsplat_science_fiction_computer_tone_chime_ping_114862"));
	RouteDecisionSound = Sound.Object;
}

void ABaseGameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RouteVoteTimer);
	Super::EndPlay(EndPlayReason);
}

bool ABaseGameState::StartRouteVote()
{
	if (!HasAuthority() || RouteVote.bActive || bGameCleared)
	{
		return false;
	}
	UWorldGenSubSystem* WorldGen = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>();
	check(WorldGen);
	if (WorldGen->IsSectorChanging())
	{
		return false;
	}
	WorldGen->GetRunSeed();
	const FVirRoute* Route = WorldGen->GetRoute();
	check(Route);
	const int32 NextRow = WorldGen->GetCurPos().Y + 1;
	if (!Route->Levels.IsValidIndex(NextRow))
	{
		return false;
	}
	const FRouteVoteState Previous = RouteVote;
	RouteVote.Candidates.Reset();
	RouteVote.Votes.Reset();
	for (int32 X = 0; X < Route->Levels[NextRow].Sectors.Num(); ++X)
	{
		const FIntPoint Node(X, NextRow);
		if (WorldGen->CanMoveTo(Node))
		{
			RouteVote.Candidates.Add(Node);
		}
	}
	RouteVote.SetPlayers(GetConnectedPlayers());
	checkf(!RouteVote.Candidates.IsEmpty(), TEXT("[RouteVote] 이동 가능한 방이 없습니다."));
	if (RouteVote.Votes.IsEmpty())
	{
		return false;
	}
	++RouteVote.Round;
	RouteVote.bActive = true;
	RouteVote.EndServerTime = GetServerWorldTimeSeconds() + RouteVoteDuration;
	GetWorldTimerManager().SetTimer(RouteVoteTimer,
		FTimerDelegate::CreateUObject(this, &ABaseGameState::ResolveRouteVote, true), RouteVoteDuration, false);
	OnRep_RouteVote(Previous);
	ForceNetUpdate();
	return true;
}

void ABaseGameState::SubmitRouteVote(APlayerController* Requester, int32 Round, FIntPoint Node)
{
	if (!HasAuthority() || !IsValid(Requester) || !RouteVote.bActive || Round != RouteVote.Round ||
		!GetConnectedPlayers().Contains(Requester->PlayerState.Get()))
	{
		return;
	}
	if (GetServerWorldTimeSeconds() >= RouteVote.EndServerTime)
	{
		ResolveRouteVote();
		return;
	}
	const FRouteVoteState Previous = RouteVote;
	if (RouteVote.Submit(Requester->PlayerState, Round, Node, GetServerWorldTimeSeconds()))
	{
		OnRep_RouteVote(Previous);
		ForceNetUpdate();
		ResolveRouteVote();
	}
}

void ABaseGameState::ResolveRouteVote(bool bTimeExpired)
{
	if (!HasAuthority() || !RouteVote.bActive)
	{
		return;
	}
	const TArray<FIntPoint> Winners = RouteVote.GetWinners(bTimeExpired || GetServerWorldTimeSeconds() >= RouteVote.EndServerTime);
	if (Winners.IsEmpty())
	{
		return;
	}
	const FIntPoint Selected = Winners[FMath::RandHelper(Winners.Num())];
	UWorldGenSubSystem* WorldGen = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>();
	check(WorldGen);
	CancelRouteVote();
	MulticastPlayRouteDecisionSound();
	WorldGen->ChangeLevel(Selected);
}

void ABaseGameState::MulticastPlayRouteDecisionSound_Implementation()
{
	if (GetNetMode() != NM_DedicatedServer && IsValid(RouteDecisionSound))
	{
		UGameplayStatics::PlaySound2D(this, RouteDecisionSound);
	}
}

void ABaseGameState::CancelRouteVote()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(RouteVoteTimer);
	RouteVote.bActive = false;
	ForceNetUpdate();
}

void ABaseGameState::UpdateRouteVoters()
{
	if (!HasAuthority() || !RouteVote.bActive)
	{
		return;
	}
	const FRouteVoteState Previous = RouteVote;
	RouteVote.SetPlayers(GetConnectedPlayers());
	OnRep_RouteVote(Previous);
	ForceNetUpdate();
	if (RouteVote.Votes.IsEmpty())
	{
		CancelRouteVote();
		return;
	}
	ResolveRouteVote();
}

void ABaseGameState::HandleRoutePlayerJoined(APlayerController* Player)
{
	if (!HasAuthority() || !RouteVote.bActive)
	{
		return;
	}
	UpdateRouteVoters();
	if (!RouteVote.bActive)
	{
		return;
	}
	if (ABasePlayerController* PC = Cast<ABasePlayerController>(Player))
	{
		UWorldGenSubSystem* WorldGen = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>();
		check(WorldGen);
		PC->Client_OpenRouteMap(WorldGen->GetRunSeed(), WorldGen->GetSheet(), WorldGen->GetCurPos(),
			WorldGen->GetVisited(), RouteVote.Round, RouteVote.EndServerTime);
	}
}

void ABaseGameState::OnRep_RouteVote(const FRouteVoteState& Previous)
{
	TArray<FIntPoint> Nodes = Previous.Candidates;
	for (FIntPoint Node : RouteVote.Candidates)
	{
		Nodes.AddUnique(Node);
	}
	for (FIntPoint Node : Nodes)
	{
		const int32 Count = RouteVote.CountVotes(Node);
		if (Previous.Round != RouteVote.Round || Previous.CountVotes(Node) != Count)
		{
			OnRouteVotesChanged.Broadcast(Node, Count);
		}
	}
}

void ABaseGameState::BeginPlay()
{
	Super::BeginPlay();
	// 초기 복제가 BeginPlay보다 먼저 도착했어도 최신 상태를 적용한다.
	ApplyMusicState();
}

void ABaseGameState::SetMusicState(float Value)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!FMath::IsFinite(Value))
	{
		UE_LOG(LogProjectMusicState, Warning, TEXT("유효하지 않은 음악 상태 값."));
		return;
	}
	MusicState.bHasState = true;
	MusicState.ParameterValue = Value;
	++MusicState.Revision;
	ForceNetUpdate();
	ApplyMusicState();
}

void ABaseGameState::OnRep_MusicState()
{
	ApplyMusicState();
}

void ABaseGameState::ApplyMusicState()
{
	if (!MusicState.bHasState || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UMusicDirectorSubsystem* Director = GameInstance->GetSubsystem<UMusicDirectorSubsystem>())
		{
			Director->SetMusicState(MusicState.ParameterValue);
		}
	}
}

void ABaseGameState::AddPlayerState(APlayerState* PlayerState)
{
	Super::AddPlayerState(PlayerState);

	OnRoomPlayersChanged.Broadcast();
	if (HasAuthority() && bClearResultsShown)
	{
		UpdateReturnReadiness();
	}
}

void ABaseGameState::RemovePlayerState(APlayerState* PlayerState)
{
	Super::RemovePlayerState(PlayerState);
	UpdateRouteVoters();

	OnRoomPlayersChanged.Broadcast();
	if (HasAuthority() && bClearResultsShown)
	{
		UpdateReturnReadiness();
	}
}

EDLHero ABaseGameState::GetLocalHero() const
{
	// PC->PlayerState는 접속 직후 비어 있다. PlayerArray에서 로컬 것을 찾는다
	for (const APlayerState* PS : PlayerArray)
	{
		const ABasePlayerState* BasePS = Cast<ABasePlayerState>(PS);
		if (BasePS && BasePS->IsLocal())
		{
			return BasePS->GetHeroType();
		}
	}
	return EDLHero::None;
}

void ABaseGameState::MulticastScreenText_Implementation(const FText& Text, EScreenIntensity Intensity, float Duration)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UEventManager>()->ShowScreenText(Text, Intensity, Duration);
	}
}

void ABaseGameState::MulticastShowClear_Implementation(const FText& Text, float Duration)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UEventManager>()->OnShowClear.Broadcast(Text, FMath::Max(0.1f, Duration));
	}
}

void ABaseGameState::MulticastTimeLimit_Implementation(float Remaining)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UEventManager>()->SetTimeLimit(Remaining);
	}
}

void ABaseGameState::MulticastGameOver_Implementation()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UEventManager>()->BroadcastGameOver();
	}
}

void ABaseGameState::SetGameCleared()
{
	if (!HasAuthority() || bGameCleared)
	{
		return;
	}

	FinalizeRunStatistics();
	bGameCleared = true;
	MulticastGameClear();
}

void ABaseGameState::StartRunStatistics()
{
	if (!HasAuthority())
	{
		return;
	}
	if (URunStatisticsSubsystem* Clock = GetGameInstance()->GetSubsystem<URunStatisticsSubsystem>())
	{
		Clock->StartIfNeeded();
	}
}

void ABaseGameState::FinalizeRunStatistics()
{
	if (!HasAuthority())
	{
		return;
	}
	URunStatisticsSubsystem* Clock = GetGameInstance()->GetSubsystem<URunStatisticsSubsystem>();
	if (!Clock || (!Clock->IsCollecting() && !Clock->IsFinalized()))
	{
		return;
	}
	ClearDurationSeconds = Clock->FinalizeDuration();
	for (APlayerState* Entry : PlayerArray)
	{
		if (ABasePlayerState* BasePS = Cast<ABasePlayerState>(Entry); BasePS && !BasePS->IsInactive())
		{
			BasePS->FinalizeRunStatistics(ClearDurationSeconds);
		}
	}
	ForceNetUpdate();
}

void ABaseGameState::ShowClearResults()
{
	if (!HasAuthority() || !bGameCleared || bClearResultsShown)
	{
		return;
	}

	bClearResultsShown = true;
	UpdateReturnReadiness();
	MulticastShowClearResults();
}

TArray<APlayerState*> ABaseGameState::GetConnectedPlayers() const
{
	TArray<APlayerState*> Players;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APlayerState* Entry = PC ? PC->PlayerState.Get() : nullptr;
		if (IsValid(Entry) && !Entry->IsInactive() && PlayerArray.Contains(Entry) &&
		    !LeavingPlayers.Contains(TWeakObjectPtr<APlayerState>(Entry)))
		{
			Players.AddUnique(Entry);
		}
	}
	return Players;
}

bool ABaseGameState::IsPlayerReturnReady(const APlayerState* InPlayer) const
{
	return InPlayer && ReturnReadyState.ReadyPlayers.Contains(InPlayer);
}

void ABaseGameState::RequestReturnToRoom(APlayerController* Requester)
{
	if (!HasAuthority() || !bGameCleared || !bClearResultsShown || ReturnReadyState.bTravelStarted ||
	    !IsValid(Requester))
	{
		return;
	}
	APlayerState* RequesterPS = Requester->PlayerState.Get();
	if (!GetConnectedPlayers().Contains(RequesterPS))
	{
		return;
	}
	ReturnReadyState.ReadyPlayers.AddUnique(RequesterPS);
	UpdateReturnReadiness();
}

void ABaseGameState::HandlePlayerLeaving(APlayerState* LeavingPlayer)
{
	if (!HasAuthority() || !LeavingPlayer)
	{
		return;
	}
	// Logout의 Super 호출 전에도 컨트롤러는 남아 있으므로 즉시 대상에서 제외한다.
	LeavingPlayers.Add(LeavingPlayer);
	UpdateRouteVoters();
	if (bClearResultsShown)
	{
		UpdateReturnReadiness();
	}
}

void ABaseGameState::UpdateReturnReadiness()
{
	if (!HasAuthority() || ReturnReadyState.bTravelStarted)
	{
		return;
	}
	const TArray<APlayerState*> Players = GetConnectedPlayers();
	ReturnReadyState.ReadyPlayers.RemoveAll(
	    [&Players](const TObjectPtr<APlayerState>& Entry)
	    {
		    return !Players.Contains(Entry.Get());
	    });
	ReturnReadyState.PlayerCount = Players.Num();
	OnRep_ReturnReadyState();
	ForceNetUpdate();
	if (Players.IsEmpty() || !bGameCleared || !bClearResultsShown)
	{
		return;
	}
	for (const APlayerState* Entry : Players)
	{
		if (!IsPlayerReturnReady(Entry))
		{
			return;
		}
	}
	ABaseGameMode* Mode = GetWorld()->GetAuthGameMode<ABaseGameMode>();
	if (!Mode)
	{
		return;
	}
	// 이동 중 Logout/중복 RPC가 들어와도 다시 시작하지 않는다.
	ReturnReadyState.bTravelStarted = true;
	OnRep_ReturnReadyState();
	ForceNetUpdate();
	if (!Mode->ReturnPartyToRoom())
	{
		ReturnReadyState.bTravelStarted = false;
		ReturnReadyState.ReadyPlayers.Reset();
		OnRep_ReturnReadyState();
		ForceNetUpdate();
	}
}

void ABaseGameState::OnRep_ReturnReadyState()
{
	OnReturnReadyChanged.Broadcast();
}

void ABaseGameState::MulticastShowClearResults_Implementation()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UEventManager* Events = GameInstance->GetSubsystem<UEventManager>())
		{
			Events->BroadcastShowClearResults();
		}
	}
}

void ABaseGameState::MulticastGameClear_Implementation()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UEventManager>()->BroadcastGameClear();
	}
}

void ABaseGameState::MulticastBossBattleHUD_Implementation(ABossMonsterCharacter* Boss, const bool bShow)
{
	bSharedEncounterHealth = false;
	if (HasAuthority())
	{
		DisplayedSplitHealth = nullptr;
		ForceNetUpdate();
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UEventManager>()->BroadcastBossBattleHUD(Boss, bShow);
	}
}

void ABaseGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABaseGameState, bSharedEncounterHealth);
	DOREPLIFETIME(ABaseGameState, SharedEncounterHealth);
	DOREPLIFETIME(ABaseGameState, SharedEncounterMaxHealth);
	DOREPLIFETIME(ABaseGameState, SharedEncounterTraits);
	DOREPLIFETIME(ABaseGameState, bGameCleared);
	DOREPLIFETIME(ABaseGameState, ReturnReadyState);
	DOREPLIFETIME(ABaseGameState, ClearDurationSeconds);
	DOREPLIFETIME(ABaseGameState, Sector);
	DOREPLIFETIME(ABaseGameState, SectorPlacements);
	DOREPLIFETIME(ABaseGameState, PlacementSerial);
	DOREPLIFETIME(ABaseGameState, MusicState);
	DOREPLIFETIME(ABaseGameState, RouteVote);
}

void ABaseGameState::SetSector(TSoftObjectPtr<UWorld> Level, FIntPoint Pos, int32 Retry)
{
	if (!HasAuthority())
	{
		return;
	}

	Sector.Level = Level;
	Sector.Pos = Pos;
	Sector.Retry = Retry;
	Sector.Serial += 1;

	OnRep_Sector(); // 서버는 OnRep 이 안 뜬다
}

void ABaseGameState::ResendSectorPlacements()
{
	if (!HasAuthority())
	{
		return;
	}

	PlacementSerial += 1;
	OnRep_SectorPlacements(); // 서버는 OnRep 이 안 뜬다
}

void ABaseGameState::OnRep_Sector()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UWorldGenSubSystem>()->ApplySector(Sector.Level, Sector.Pos, Sector.Retry);
	}
}

void ABaseGameState::SetSectorPlacements(const TArray<FSectorPlacement>& Placements)
{
	if (!HasAuthority())
	{
		return;
	}

	SectorPlacements = Placements;
	PlacementSerial += 1;

	OnRep_SectorPlacements(); // 서버는 OnRep 이 안 뜬다
}

void ABaseGameState::OnRep_SectorPlacements()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UWorldGenSubSystem* WorldGen = GameInstance->GetSubsystem<UWorldGenSubSystem>();
	WorldGen->UnloadLastSubLevel();

	USectorSubsystem* SectorSub = GameInstance->GetSubsystem<USectorSubsystem>();
	SectorSub->SpawnPlacements(GetWorld(), SectorPlacements);

	// 빈 배치라도 전환은 끝내야 한다. 여기서 빠지면 검은 화면과 로딩창이 영영 안 걷힌다
	WorldGen->FinishTransition(SectorSub->GetEntryTransform());
}

void ABaseGameState::MulticastEncounterBattleHUD(ACommonEnemyCharacter* Enemy, bool bShow)
{
	if (!HasAuthority())
	{
		return;
	}
	DisplayedSplitHealth = bShow && IsValid(Enemy) ? Enemy->GetSharedHealthPool() : nullptr;
	bSharedEncounterHealth = IsValid(DisplayedSplitHealth);
	if (bSharedEncounterHealth)
	{
		RefreshSharedEncounterHealth(DisplayedSplitHealth);
	}
	ForceNetUpdate();
	MulticastEncounterHUDState(Enemy, bShow, bSharedEncounterHealth, SharedEncounterHealth, SharedEncounterMaxHealth,
	                           SharedEncounterTraits);
}

void ABaseGameState::MulticastEncounterHUDState_Implementation(ACommonEnemyCharacter* Enemy, bool bShow, bool bShared,
                                                               float Health, float MaxHealth, const FText& Traits)
{
	bSharedEncounterHealth = bShared;
	SharedEncounterHealth = Health;
	SharedEncounterMaxHealth = MaxHealth;
	SharedEncounterTraits = Traits;
	if (auto* GI = GetGameInstance())
	{
		GI->GetSubsystem<UEventManager>()->OnEncounterBattleHUDRequested.Broadcast(Enemy, bShow);
	}
}

void ABaseGameState::RefreshSharedEncounterHealth(UEnemySplitHealthState* State)
{
	if (!HasAuthority() || !State || DisplayedSplitHealth != State)
	{
		return;
	}
	if (!State->HasMembers())
	{
		DisplayedSplitHealth = nullptr;
		bSharedEncounterHealth = false;
	}
	SharedEncounterHealth = State->GetHealth();
	SharedEncounterMaxHealth = State->GetMaxHealth();
	SharedEncounterTraits = State->GetTraitDisplayName();
	ForceNetUpdate();
}
