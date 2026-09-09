#include "WorldGenSubSystem.h"

#include "MapStatic.h"
#include "MapStruct.h"
#include "Engine/World.h"

#include "WorldGenSettings.h"

void UWorldGenSubSystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);

	// Settings에서 값을 가져와 하위 시스템 초기화
	const UWorldGenSettings *Settings = GetDefault<UWorldGenSettings>();

	// 내부 변수들 초기화
	SetupVars();

	if (UClass *LoadedClass = Settings->RouteWidgetClass.LoadSynchronous())
	{
		RouteWidgetClass = LoadedClass;
	}

	// route생성시스템
	routeMaker.Initialize(Settings->DefaultWeights);

	// true일 시 sector생성시스템이 static map들을 사용하도록 변경
	isDebug = Settings->isDebug;

	if (isDebug)
	{
		TMap<ESectorType, TArray<FName>> LoadedLevels;
		for (const auto &Pair : Settings->GenLevelNames)
		{
			LoadedLevels.Add(Pair.Key, Pair.Value.LevelNames);
		}
		sectorMaker.DebugInitialize(MoveTemp(LoadedLevels));
	}
	else
	{
		TMap<ESectorType, TArray<TSubclassOf<ASector>>> LoadedNodes;
		for (const auto &Pair : Settings->GenNodes)
		{
			TArray<TSubclassOf<ASector>> curNodes;
			LoadClassArray(Pair.Value.Nodes, curNodes, TEXT("GenNode"));
			LoadedNodes.Add(Pair.Key, MoveTemp(curNodes));
		}
		sectorMaker.Initialize(MoveTemp(LoadedNodes));
	}
}

void UWorldGenSubSystem::SetupVars()
{
	RouteIndex = 0;
}

void UWorldGenSubSystem::Deinitialize()
{
	for (FVirRoute *Route : virtualRoutes)
	{
		delete Route;
	}
	virtualRoutes.Empty();

	Super::Deinitialize();
}

int32 UWorldGenSubSystem::GetRunSeed()
{
	if (!bRouteReady)
	{
		// 재현이 필요하면 World Gen 설정에 값을 박음
		const int32 ConfiguredSeed = GetDefault<UWorldGenSettings>()->Seed;
		EnsureRoute(ConfiguredSeed != 0 ? ConfiguredSeed : FMath::Rand());
	}
	return RunSeed;
}

void UWorldGenSubSystem::EnsureRoute(int32 Seed)
{
	if (bRouteReady && RunSeed == Seed)
	{
		return; // 런당 한 번. 트래블로 재진입해도 다시 만들지 않음
	}

	RunSeed = Seed;
	randStream.Initialize(Seed);

	routeMaker.generateRoute(virtualRoutes[RouteIndex]); // 객체는 멤버 초기화에서 할당되고 Deinitialize에서 해제된다
	curPos = FIntPoint(0, -1);
	visitedPath.Reset(); // 새 루트면 지나온 기록도 버린다
	bRouteReady = true;

	DebugRoute();
}

bool UWorldGenSubSystem::CanMoveTo(FIntPoint pos) const
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

void UWorldGenSubSystem::ChangeLevel(FIntPoint pos)
{
	UWorld *World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return; // 트래블은 서버만 시작할 수 있다
	}

	if (!CanMoveTo(pos))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChangeLevel] 거부: (%d,%d) -> (%d,%d)"), curPos.X, curPos.Y, pos.X, pos.Y);
		return;
	}

	if (TravelStartedIn == World)
	{
		return; // 먼저 누른 사람이 이긴다
	}
	TravelStartedIn = World;

	// virtualRoutes[RouteIndex]->DefaultEnvType 애도 넘겨서 환경에 따라 다른 지형 생성
	const ESectorType Type = GetRoute()->Levels[pos.Y].Sectors[pos.X].Type;
	UE_LOG(LogTemp, Warning, TEXT("ChangeLevel : %d, %d"), pos.Y, pos.X);
	curPos = pos;
	visitedPath.Add(pos);

	if (isDebug)
	{
		sectorMaker.MakeDebugSector(World, Type);
	}
	else
	{
		sectorMaker.MakeSector(World, Type);
	}
}

void UWorldGenSubSystem::DebugRoute()
{
	if (!virtualRoutes.IsValidIndex(RouteIndex) || !virtualRoutes[RouteIndex])
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugRoute] Invalid RouteIndex: %d"), RouteIndex);
		return;
	}

	const FVirRoute *Route = virtualRoutes[RouteIndex];

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[DebugRoute] RouteIndex=%d, Env=%d, RouteCount=%d, MaxX=%d, MaxY=%d"),
		RouteIndex,
		static_cast<int32>(Route->DefaultEnvType),
		Route->RouteCount,
		Route->MaxX,
		Route->MaxY);

	if (Route->Levels.Num() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DebugRoute] Route has no levels."));
		return;
	}

	for (int32 Y = Route->Levels.Num() - 1; Y >= 0; --Y)
	{
		FString RowString;

		for (int32 X = 0; X < Route->Levels[Y].Sectors.Num(); ++X)
		{
			const ESectorType Type = Route->Levels[Y].Sectors[X].Type;

			TCHAR TypeChar = TCHAR('?');
			switch (Type)
			{
			case ESectorType::None:
				TypeChar = TCHAR('.');
				break;
			case ESectorType::Allot:
				TypeChar = TCHAR('A');
				break;
			case ESectorType::Monster:
				TypeChar = TCHAR('M');
				break;
			case ESectorType::Event:
				TypeChar = TCHAR('E');
				break;
			case ESectorType::Elite:
				TypeChar = TCHAR('L');
				break;
			case ESectorType::Store:
				TypeChar = TCHAR('S');
				break;
			case ESectorType::Boss:
				TypeChar = TCHAR('B');
				break;
			default:
				break;
			}

			RowString += FString::Printf(TEXT("%c "), TypeChar);
		}

		UE_LOG(LogTemp, Warning, TEXT("[DebugRoute] Y=%02d | %s"), Y, *RowString);
	}
}
