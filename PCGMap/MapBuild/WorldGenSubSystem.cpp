#include "WorldGenSubSystem.h"

#include "MapStatic.h"
#include "MapStruct.h"
#include "Engine/World.h"

#include "WorldGenSettings.h"
#include "WorldGenSheet.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/PlayerStart.h"
#include "GameCore/BasePlayerController.h"
#include "GameCore/BaseGameState.h"
#include "GameCore/BaseGameMode.h"
#include "Environment/JumpMapLayoutController.h"
#include "GameCore/BasePlayerState.h"
#include "GameCore/Character/PlayerCharacter.h"
#include "GameCore/Match/RunStatisticsSubsystem.h"
#include "SectorBuild/SectorSubsystem.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogJumpMapTransition, Log, All);

// 인스턴스 이름을 노드 좌표로 고정한다. 서버와 클라가 다른 이름을 쓰면
// 그 안에 배치된 액터의 복제 참조가 서로 안 맞는다
// 재도전이 아니면 좌표만 쓴다. 이름이 양쪽에서 같아야 안에 배치된 액터도 같은 이름을 갖는다
static FString MakeSectorInstanceName(FIntPoint Pos, int32 Retry = 0)
{
	return Retry > 0
		? FString::Printf(TEXT("Sector_%d_%d_R%d"), Pos.X, Pos.Y, Retry)
		: FString::Printf(TEXT("Sector_%d_%d"), Pos.X, Pos.Y);
}

static FVector MakeSectorOffset(FIntPoint Pos)
{
	const float Interval = GetDefault<UWorldGenSettings>()->SectorInterval;
	return FVector(Pos.X * Interval, Pos.Y * Interval, 0.f);
}

void UWorldGenSubSystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);

	// Settings에서 값을 가져와 하위 시스템 초기화
	const UWorldGenSettings *Settings = GetDefault<UWorldGenSettings>();

	// 내부 변수들 초기화
	SetupVars();

	LoadEnvLevelSets();

	if (UClass *LoadedClass = Settings->RouteWidgetClass.LoadSynchronous())
	{
		RouteWidgetClass = LoadedClass;
	}

	isDebug = Settings->isDebug;

	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UWorldGenSubSystem::OnPreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UWorldGenSubSystem::OnPostLoadMap);
}

void UWorldGenSubSystem::OnPreLoadMap(const FString& /*MapName*/)
{
	ClearJumpMapPreparation();
	// 죽은 월드의 스트리밍 객체를 들고 있으면 다음 선택이 프리로드 검사에서 막힌다
	PreloadedSectors.Reset();
	CurrentSector = nullptr;
	isLastSubLevel = false;
	PendingSector = nullptr;
	bSectorChanging = false;
	RetryCount = 0;
}

void UWorldGenSubSystem::OnPostLoadMap(UWorld* /*LoadedWorld*/)
{
	PreloadReachableSectors();
}

void UWorldGenSubSystem::LoadEnvLevelSets()
{
	EnvLevelSets.Reset();

	const UEnum* EnvEnum = StaticEnum<EEnvironmentType>();
	for (int32 i = 0; i < EnvEnum->NumEnums() - 1; i++)
	{
		const FString EnvName = EnvEnum->GetNameStringByIndex(i);
		const FString AssetName = SectorLevelSetPrefix + EnvName;
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *SectorLevelSetDir, *AssetName, *AssetName);

		if (USectorLevelSet* Set = LoadObject<USectorLevelSet>(nullptr, *ObjectPath))
		{
			EnvLevelSets.Add(static_cast<EEnvironmentType>(EnvEnum->GetValueByIndex(i)), Set);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SectorLevelSet] 없음: %s"), *ObjectPath);
		}
	}
}

const TArray<TSoftObjectPtr<UWorld>>* UWorldGenSubSystem::FindSectorLevels(EEnvironmentType Env, ESectorType Type) const
{
	const TObjectPtr<USectorLevelSet>* Set = EnvLevelSets.Find(Env);
	return Set && *Set ? (*Set)->Find(Type) : nullptr;
}

void UWorldGenSubSystem::SetupVars()
{
	RouteIndex = 0;
}

void UWorldGenSubSystem::Deinitialize()
{
	ClearJumpMapPreparation();
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	virtualRoutes.Empty();

	Super::Deinitialize();
}

int32 UWorldGenSubSystem::GetRunSeed()
{
	if (!bRouteReady)
	{
		// 재현이 필요하면 World Gen 설정에 값을 박음
		const int32 ConfiguredSeed = GetDefault<UWorldGenSettings>()->Seed;
		// 런마다 새로 읽는다. 시트를 고치면 다음 런부터 반영된다
		EnsureRoute(ConfiguredSeed != 0 ? ConfiguredSeed : FMath::Rand(), LoadWorldGenSheet());
	}
	return RunSeed;
}

void UWorldGenSubSystem::EnsureRoute(int32 Seed, const FWorldGenSheet& InSheet)
{
	if (bRouteReady && RunSeed == Seed)
	{
		return; // 런당 한 번. 트래블로 재진입해도 다시 만들지 않음
	}

	RunSeed = Seed;
	Sheet = InSheet;
	randStream.Initialize(Seed);

	virtualRoutes[RouteIndex] = FVirRoute(RouteIndex, Sheet.RouteCount, Sheet.RouteMaxX, Sheet.RouteMaxY);
	routeMaker.Initialize(Sheet.Weights);

	FVirRoute *RoutePtr = &virtualRoutes[RouteIndex];
	routeMaker.generateRoute(RoutePtr);
	curPos = FIntPoint(0, -1);
	visitedPath.Reset(); // 새 루트면 지나온 기록도 버린다
	PreloadedSectors.Reset(); // 이전 루트에서 올려둔 것은 버린다
	bRouteReady = true;

	// 0층도 미리 올려둔다. 여기서 안 하면 첫 선택이 ChangeLevel 의 프리로드 검사에 걸려 조용히 막힌다.
	// 클라에서는 안쪽에서 빠진다
	PreloadReachableSectors();
}

bool UWorldGenSubSystem::CanMoveTo(FIntPoint pos)
{
	const FVirRoute *Route = GetRoute();
	if (!Route || !Route->Levels.IsValidIndex(pos.Y) || !Route->Levels[pos.Y].Sectors.IsValidIndex(pos.X))
	{
		return false;
	}
	if (Route->Levels[pos.Y].Sectors[pos.X].Type == ESectorType::None)
	{
		return false;
	}

	if (curPos.Y < 0)
	{
		return pos.Y == 0; // 시작 지점은 0층 아무 노드나
	}
	if (pos.Y != curPos.Y + 1)
	{
		return false; // 한 층씩만
	}
	return Route->Levels[curPos.Y].Sectors[curPos.X].ConnectedSectors.Contains(pos.X);
}

void UWorldGenSubSystem::PreloadReachableSectors()
{
	UWorld *World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !bRouteReady)
	{
		return;
	}

	for (int32 X = 0; X < Sheet.RouteMaxX; X++)
	{
		const FIntPoint Pos(X, curPos.Y + 1);
		if (!CanMoveTo(Pos) || PreloadedSectors.Contains(Pos))
		{
			continue;
		}

		const FVirSector &Sector = GetRoute()->Levels[Pos.Y].Sectors[Pos.X];
		const TArray<TSoftObjectPtr<UWorld>> *Levels = FindSectorLevels(Sector.EnvType, Sector.Type);
		if (!Levels)
		{
			UE_LOG(LogTemp, Error, TEXT("[Preload] 등록된 레벨 없음: Env=%d Type=%d"),
				   (int32)Sector.EnvType, (int32)Sector.Type);
			continue;
		}

		const TSoftObjectPtr<UWorld> &Picked = (*Levels)[randStream.RandRange(0, Levels->Num() - 1)];

		// 올린 뒤에 끄면 한 프레임이라도 보일 수 있다. 처음부터 안 보이게 만든다
		const FString InstanceName = MakeSectorInstanceName(Pos);
		ULevelStreamingDynamic::FLoadLevelInstanceParams Params(
			World,
			Picked.ToSoftObjectPath().GetLongPackageName(),
			FTransform(MakeSectorOffset(Pos))); // 노드 좌표로 자리를 잡아 서로 겹치지 않게 한다
		Params.OptionalLevelNameOverride = &InstanceName;
		Params.bInitiallyVisible = false;

		bool bSuccess = false;
		ULevelStreamingDynamic *Streaming = ULevelStreamingDynamic::LoadLevelInstance(Params, bSuccess);

		if (!bSuccess || !Streaming)
		{
			UE_LOG(LogTemp, Error, TEXT("[Preload] 스트리밍 실패: (%d,%d)"), Pos.X, Pos.Y);
			continue;
		}

		FPreloadedSector &Entry = PreloadedSectors.Add(Pos);
		Entry.Streaming = Streaming;
		Entry.Level = Picked;
	}
}

void UWorldGenSubSystem::ChangeLevel(FIntPoint pos)
{
	UWorld *World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return; // 전환은 서버만 시작할 수 있다
	}

	if (!CanMoveTo(pos))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChangeLevel] 거부: (%d,%d) -> (%d,%d)"), curPos.X, curPos.Y, pos.X, pos.Y);
		return;
	}

	const FVirSector &Sector = GetRoute()->Levels[pos.Y].Sectors[pos.X];

	// 몬스터 방은 통짜 레벨 대신 섹터를 조립한다
	const bool bBuildSectors = Sector.Type == ESectorType::Monster;

	// 프리로드가 안 됐으면 갈 곳이 없다. 아무 상태도 건드리기 전에 빠진다
	const FPreloadedSector *Found = bBuildSectors ? nullptr : PreloadedSectors.Find(pos);
	if (!bBuildSectors && (!Found || !Found->Streaming))
	{
		UE_LOG(LogTemp, Error, TEXT("[ChangeLevel] 프리로드 없음: (%d,%d)"), pos.X, pos.Y);
		return;
	}

	if (bSectorChanging)
	{
		return;
	}
	bSectorChanging = true;

	UE_LOG(LogTemp, Warning, TEXT("ChangeLevel : %d, %d"), pos.Y, pos.X);
	curPos = pos;
	visitedPath.Add(pos);
	RetryCount = 0;

	EnterSector(pos, 0);
}

void UWorldGenSubSystem::RetryCurrentSector()
{
	UWorld *World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (curPos.Y < 0 || bSectorChanging)
	{
		return; // 아직 아무 데도 안 들어갔거나 이미 전환 중이다
	}

	bSectorChanging = true;
	RetryCount++;

	UE_LOG(LogTemp, Warning, TEXT("[Retry] (%d,%d) %d 번째"), curPos.X, curPos.Y, RetryCount);

	// 통짜 레벨은 내려야 같은 방이 두 겹으로 서지 않는다. 조립 방은 다시 세울 때 알아서 지운다
	if (!IsJumpMap(LastSectorLevel))
	{
		UnloadLastSubLevel();
	}

	EnterSector(curPos, RetryCount);
}

void UWorldGenSubSystem::AbandonRun()
{
	UWorld *World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}

	GetGameInstance()->GetSubsystem<URunStatisticsSubsystem>()->ResetRun();
	if (ABaseGameState* GS = World->GetGameState<ABaseGameState>())
	{
		for (APlayerState* Entry : GS->PlayerArray)
		{
			if (ABasePlayerState* BasePS = Cast<ABasePlayerState>(Entry))
			{
				BasePS->ResetRunStatistics();
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[Run] 포기 — (%d,%d) 에서 %d 칸 지나옴"),
		   curPos.X, curPos.Y, visitedPath.Num());
	ResetRunForLobby();
}

void UWorldGenSubSystem::ResetRunForLobby()
{
	if (UWorld* World = GetWorld())
	{
		if (ABaseGameState* GS = World->GetGameState<ABaseGameState>())
		{
			GS->CancelRouteVote();
		}
	}
	ClearJumpMapPreparation();
	GetGameInstance()->GetSubsystem<URunStatisticsSubsystem>()->ResetRun();

	// bRouteReady 를 내려야 다음 GetRunSeed 가 시드를 새로 뽑고 루트를 다시 만든다
	bRouteReady = false;
	RunSeed = 0;
	curPos = FIntPoint(0, -1);
	visitedPath.Reset();

	RetryCount = 0;
	LastSectorLevel = nullptr;
	PreloadedSectors.Reset();
	CurrentSector = nullptr;
	isLastSubLevel = false;
	PendingSector = nullptr;
	bSectorChanging = false;
}

void UWorldGenSubSystem::EnterSector(FIntPoint Pos, int32 Retry)
{
	UWorld *World = GetWorld();
	if (!World)
	{
		return;
	}
	ABaseGameState* GameState = World->GetGameState<ABaseGameState>();
	check(GameState);
	GameState->CancelRouteVote();

	const FVirSector &Sector = GetRoute()->Levels[Pos.Y].Sectors[Pos.X];
	const bool bBuildSectors = Sector.Type == ESectorType::Monster;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController *BPC = Cast<ABasePlayerController>(It->Get()))
		{
			BPC->Client_BeginLevelTransition(LevelFadeOutTime);
		}
	}

	// 재도전이면 프리로드가 이미 소진됐다. 그때는 들고 있던 레벨을 그대로 다시 쓴다
	const FPreloadedSector *Found = PreloadedSectors.Find(Pos);
	const TSoftObjectPtr<UWorld> Level =
		bBuildSectors ? nullptr : (Found ? Found->Level : LastSectorLevel);

	if (!bBuildSectors)
	{
		LastSectorLevel = Level;
	}

	const EEnvironmentType Env = Sector.EnvType;

	// 페이드가 다 내려간 뒤에 켠다. 켜는 것은 GameState 복제를 타고 전원에게 간다
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(
		Handle,
		FTimerDelegate::CreateWeakLambda(this, [this, Level, Pos, Env, bBuildSectors, Retry]()
		{
			UWorld *TimerWorld = GetWorld();
			ABaseGameState *GS = TimerWorld ? TimerWorld->GetGameState<ABaseGameState>() : nullptr;

			if (bBuildSectors)
			{
				// 재도전은 같은 방이 같은 모양으로 서야 한다. 다시 굴리면 다른 맵이 나온다
				if (Retry > 0 && GS)
				{
					GS->ResendSectorPlacements();
					return;
				}

				BuildSector(Env, Pos);
				return;
			}

			if (GS)
			{
				GS->SetSector(Level, Pos, Retry);
			}
		}),
		LevelFadeOutTime,
		false);
}

void UWorldGenSubSystem::BuildSector(EEnvironmentType Env, FIntPoint Pos)
{
	UWorld *World = GetWorld();
	ABaseGameState *GS = World ? World->GetGameState<ABaseGameState>() : nullptr;
	if (!GS)
	{
		// 여기서 그냥 빠지면 아무도 전환을 끝내주지 않는다
		FinishTransition(FTransform(MakeSectorOffset(Pos)));
		return;
	}

	//서버 조립
	USectorSubsystem *SectorSub = GetGameInstance()->GetSubsystem<USectorSubsystem>();
	TArray<FSectorPlacement> Placements = SectorSub->BuildSectors(World, Env, FTransform(MakeSectorOffset(Pos)), Sheet);

	
	//클라 조립
	if (Placements.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[BuildSector] 조립 실패: (%d,%d). 전환만 끝낸다"), Pos.X, Pos.Y);
	}

	// 실패해도 내려보낸다. 복제가 가야 클라도 검은 화면을 걷는다
	GS->SetSectorPlacements(Placements);
}

void UWorldGenSubSystem::UnloadLastSubLevel()
{
	if (!isLastSubLevel)
	{
		return;
	}
	isLastSubLevel = false;

	checkf(CurrentSector, TEXT("[SubLevel] 등록 표시는 있는데 서브 레벨이 없음"));
	CurrentSector->SetIsRequestingUnloadAndRemoval(true);
	CurrentSector = nullptr;
}

void UWorldGenSubSystem::ApplySector(TSoftObjectPtr<UWorld> Level, FIntPoint Pos, int32 Retry)
{
	UWorld *World = GetWorld();

	if (!World || Level.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[ApplySector] 중단 — World 없음=%d, Level 비었음=%d"),
			   World == nullptr, Level.IsNull());
		return;
	}

	ClearJumpMapPreparation();
	bPendingJumpMap = IsJumpMap(Level);
	if (bPendingJumpMap)
	{
		bSectorChanging = true;
		JumpMapWorld = World;
		// 레벨 표시 자체가 끝나지 않는 경우도 기존 파티 복귀로 처리한다.
		World->GetTimerManager().SetTimer(JumpMapTimeoutHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				FailJumpMapTransition(TEXT("점프맵 로드/배치 준비가 30초 안에 끝나지 않았습니다."));
			}), 30.f, false);
	}

	// 통짜 레벨로 가는 길이다. 직전 전투 맵의 조립 조각이 남아 있으면 여기서 치운다
	if (UGameInstance *GI = GetGameInstance(); GI && !bPendingJumpMap)
	{
		GI->GetSubsystem<USectorSubsystem>()->ClearSpawned();
	}

	ULevelStreamingDynamic *Target = nullptr;

	// 재도전은 프리로드가 이미 소진됐고, 새 이름으로 다시 올려야 한다
	const FPreloadedSector *Found = Retry > 0 ? nullptr : PreloadedSectors.Find(Pos);

	if (Found)
	{
		Target = Found->Streaming; // 서버는 미리 올려뒀다
		PreloadedSectors.Remove(Pos);
	}
	else
	{
		// 클라는 여기서 처음 올린다. 이름은 서버와 같아야 한다
		bool bSuccess = false;
		Target = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
			World, Level, MakeSectorOffset(Pos), FRotator::ZeroRotator, bSuccess,
			MakeSectorInstanceName(Pos, Retry));

		if (!bSuccess)
		{
			UE_LOG(LogTemp, Error, TEXT("[ApplySector] 스트리밍 실패: (%d,%d)"), Pos.X, Pos.Y);
			if (bPendingJumpMap)
			{
				FailJumpMapTransition(TEXT("점프맵 레벨 스트리밍에 실패했습니다."));
			}
			return;
		}
	}

	if (!Target)
	{
		if (bPendingJumpMap)
		{
			FailJumpMapTransition(TEXT("점프맵 스트리밍 객체가 없습니다."));
		}
		return;
	}

	if (PendingSector)
	{
		PendingSector->OnLevelShown.RemoveDynamic(this, &UWorldGenSubSystem::OnSectorShown);
	}
	PendingSector = Target;
	Target->OnLevelShown.AddDynamic(this, &UWorldGenSubSystem::OnSectorShown);
	Target->SetShouldBeVisible(true);
}

void UWorldGenSubSystem::OnSectorShown()
{
	UWorld *World = GetWorld();

	if (!World || !PendingSector)
	{
		return;
	}

	PendingSector->OnLevelShown.RemoveDynamic(this, &UWorldGenSubSystem::OnSectorShown);

	const ULevel *Level = PendingSector->GetLoadedLevel();
	if (bPendingJumpMap)
	{
		APlayerStart* JumpStart = nullptr;
		AJumpMapLayoutController* LayoutController = nullptr;
		int32 ControllerCount = 0;
		if (Level)
		{
			for (AActor* Actor : Level->Actors)
			{
				if (!IsValid(Actor))
				{
					continue;
				}
				if (!JumpStart)
				{
					JumpStart = Cast<APlayerStart>(Actor);
				}
				if (AJumpMapLayoutController* Candidate = Cast<AJumpMapLayoutController>(Actor))
				{
					LayoutController = Candidate;
					++ControllerCount;
				}
			}
		}
		if (!JumpStart || ControllerCount != 1)
		{
			FailJumpMapTransition(FString::Printf(TEXT("PlayerStart 또는 점프맵 컨트롤러 설정 오류: 시작점=%d 컨트롤러=%d"),
				JumpStart != nullptr, ControllerCount));
			return;
		}
		JumpMapEntry = JumpStart->GetActorTransform();
		AwaitedJumpMapController = LayoutController;
		LayoutController->OnLayoutFinished.AddUObject(this, &ThisClass::OnJumpMapLayoutFinished);
		LayoutController->OnParticipantsReady.AddUObject(this, &ThisClass::OnJumpMapParticipantsReady);
		LayoutController->BeginLayout();
		return;
	}

	UnloadLastSubLevel();
	CurrentSector = PendingSector;
	isLastSubLevel = true;
	PendingSector = nullptr;
	bSectorChanging = false;
	AActor *Start = nullptr;
	for (AActor *Actor : Level->Actors)
	{
		if (Actor && Actor->IsA<APlayerStart>())
		{
			Start = Actor;
			break;
		}
	}

	if (!Start)
	{
		UE_LOG(LogTemp, Error, TEXT("[ChangeLevel] PlayerStart 없음: (%d,%d)"), curPos.X, curPos.Y);
	}

	FinishTransition(Start ? Start->GetActorTransform() : FTransform::Identity);
}

void UWorldGenSubSystem::FinishTransition(const FTransform &Entry)
{
	UWorld *World = GetWorld();

	if (!World)
	{
		return;
	}

	AJumpMapLayoutController* JumpController = AwaitedJumpMapController.Get();
	const bool bFinishJumpMap = JumpController && JumpMapWorld.Get() == World;
	if (!bFinishJumpMap)
	{
		bSectorChanging = false;
	}

	// 검은 화면은 각자 자기 머신이 걷는다. 남의 로드가 끝나길 기다릴 이유가 없다
	if (!bFinishJumpMap)
	{
		if (ABasePlayerController *Local = Cast<ABasePlayerController>(World->GetFirstPlayerController()))
		{
			Local->EndLevelTransition();
		}
	}

	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	// 두 경로가 여기서 만난다. OnSectorShown 쪽에만 두면 조립 맵에 다녀온 뒤 다음 층이 안 올라간다
	if (!bFinishJumpMap)
	{
		PreloadReachableSectors();
	}

	int32 Index = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn *Pawn = It->IsValid() ? It->Get()->GetPawn() : nullptr;
		if (!Pawn)
		{
			const APlayerState* PlayerState = It->IsValid() ? It->Get()->PlayerState.Get() : nullptr;
			if (bFinishJumpMap && IsValid(PlayerState) && !PlayerState->IsInactive()
				&& !PlayerState->IsOnlyASpectator() && !PlayerState->IsABot())
			{
				FailJumpMapTransition(TEXT("점프맵 참가자의 Pawn이 없어 시작 위치로 이동할 수 없습니다."));
				return;
			}
			continue;
		}

		// 포탈에서 걸어둔 무적을 여기서 걷는다. 트래블 때는 ASC 가 사라지며 같이 없어졌다
		if (UAbilitySystemComponent *ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn))
		{
			ASC->RemoveLooseGameplayTag(GameplayTagSystem::State_Invincible);
		}

		// 같은 지점에 겹쳐 놓으면 서로 밀어낸다. 한 줄로 벌려 둔다
		const FVector Target =
			Entry.GetLocation() + Entry.GetRotation().GetRightVector() * (Index++ * 150.f);

		// 막힌 자리로 보내면 조용히 실패하고 폰이 있던 데 남는다
		if (!Pawn->TeleportTo(Target, Entry.Rotator()))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Transition] 텔레포트 실패 — 목표 %s, 폰은 %s 에 남음"),
				   *Target.ToCompactString(), *Pawn->GetActorLocation().ToCompactString());
			if (bFinishJumpMap)
			{
				FailJumpMapTransition(TEXT("점프맵 시작 위치로 이동하지 못했습니다."));
				return;
			}
		}
		else if (APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(Pawn))
		{
			// 이전 맵의 체크포인트를 새 맵의 실제 입장 위치로 교체한다.
			PlayerCharacter->SetCheckpoint(PlayerCharacter->GetActorLocation());
		}
	}

	if (bFinishJumpMap)
	{
		PreloadReachableSectors();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get()))
			{
				// 같은 PC의 Reliable RPC 순서로 위치를 먼저 맞춘 뒤 로딩을 종료한다.
				if (APawn* Pawn = PC->GetPawn())
				{
					PC->ClientSetLocation(Pawn->GetActorLocation(), Pawn->GetActorRotation());
				}
				PC->Client_FinishJumpMapLoading(JumpController);
			}
		}
		bSectorChanging = false;
		ClearJumpMapPreparation();
	}
}

bool UWorldGenSubSystem::IsJumpMap(const TSoftObjectPtr<UWorld>& Level) const
{
	if (Level.IsNull())
	{
		return false;
	}
	const FSoftObjectPath Path = Level.ToSoftObjectPath();
	for (const TSoftObjectPtr<UWorld>& JumpMap : GetDefault<UWorldGenSettings>()->JumpMapLevels)
	{
		if (!JumpMap.IsNull() && JumpMap.ToSoftObjectPath() == Path)
		{
			return true;
		}
	}
	return false;
}

void UWorldGenSubSystem::OnJumpMapLayoutFinished(AJumpMapLayoutController* LayoutController, bool bSuccess)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(LayoutController) || AwaitedJumpMapController.Get() != LayoutController
		|| JumpMapWorld.Get() != World || bJumpMapTransitionFailed)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get()); PC && PC->IsLocalController())
		{
			PC->Server_ReportJumpMapReady(LayoutController, bSuccess && IsValid(PC->GetPawn()));
		}
	}
}

void UWorldGenSubSystem::ReportJumpMapReady(ABasePlayerController* Player, AJumpMapLayoutController* LayoutController, bool bSuccess)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsValid(Player) || !IsValid(LayoutController)
		|| AwaitedJumpMapController.Get() != LayoutController || JumpMapWorld.Get() != World
		|| bJumpMapTransitionFailed)
	{
		return;
	}
	LayoutController->ReportParticipantReady(Player, bSuccess);
}

void UWorldGenSubSystem::OnJumpMapParticipantsReady(AJumpMapLayoutController* LayoutController, bool bSuccess)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsValid(LayoutController)
		|| AwaitedJumpMapController.Get() != LayoutController || JumpMapWorld.Get() != World
		|| bJumpMapTransitionFailed)
	{
		return;
	}
	if (!bSuccess)
	{
		FailJumpMapTransition(LayoutController->GetPreparationFailure());
		return;
	}
	// 기다리는 동안 이전 발판을 유지하고, 전원 준비 완료 뒤 기존 정리를 수행한다.
	GetGameInstance()->GetSubsystem<USectorSubsystem>()->ClearSpawned();
	UnloadLastSubLevel();
	CurrentSector = PendingSector;
	isLastSubLevel = true;
	PendingSector = nullptr;
	FinishTransition(JumpMapEntry);
}

void UWorldGenSubSystem::CompleteJumpMapTransition(AJumpMapLayoutController* LayoutController)
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(LayoutController) || AwaitedJumpMapController.Get() != LayoutController
		|| JumpMapWorld.Get() != World || !LayoutController->IsLocalLayoutReady() || bJumpMapTransitionFailed)
	{
		return;
	}
	bSectorChanging = false;
	if (World->GetNetMode() == NM_Client)
	{
		GetGameInstance()->GetSubsystem<USectorSubsystem>()->ClearSpawned();
		UnloadLastSubLevel();
		CurrentSector = PendingSector;
		isLastSubLevel = true;
		PendingSector = nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get()); PC && PC->IsLocalController())
		{
			PC->EndLevelTransition();
		}
	}
	ClearJumpMapPreparation();
}

void UWorldGenSubSystem::FailJumpMapTransition(const FString& Reason)
{
	UWorld* World = GetWorld();
	if (!World || JumpMapWorld.Get() != World || bJumpMapTransitionFailed)
	{
		return;
	}
	bJumpMapTransitionFailed = true;
	UE_LOG(LogJumpMapTransition, Error, TEXT("점프맵 준비 실패: %s"), *Reason);
	World->GetTimerManager().ClearTimer(JumpMapTimeoutHandle);
	if (World->GetNetMode() == NM_Client)
	{
		if (AJumpMapLayoutController* LayoutController = AwaitedJumpMapController.Get())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get()); PC && PC->IsLocalController())
				{
					PC->Server_ReportJumpMapReady(LayoutController, false);
				}
			}
		}
		return;
	}
	ABaseGameMode* GameMode = World->GetAuthGameMode<ABaseGameMode>();
	if (!GameMode || !GameMode->ReturnPartyToRoom())
	{
		UE_LOG(LogJumpMapTransition, Error, TEXT("점프맵 실패 후 대기실 복귀를 시작하지 못했습니다."));
	}
}

void UWorldGenSubSystem::ClearJumpMapPreparation()
{
	if (PendingSector)
	{
		PendingSector->OnLevelShown.RemoveDynamic(this, &UWorldGenSubSystem::OnSectorShown);
	}
	if (UWorld* World = JumpMapWorld.Get())
	{
		World->GetTimerManager().ClearTimer(JumpMapTimeoutHandle);
	}
	if (AJumpMapLayoutController* LayoutController = AwaitedJumpMapController.Get())
	{
		LayoutController->OnLayoutFinished.RemoveAll(this);
		LayoutController->OnParticipantsReady.RemoveAll(this);
		LayoutController->CancelPreparation();
	}
	AwaitedJumpMapController.Reset();
	JumpMapWorld.Reset();
	bPendingJumpMap = false;
	bJumpMapTransitionFailed = false;
}
