#include "RouteGenSystem.h"

#include "TeamProject_Fin/PCGMap/MapBuild/MapStatic.h"

namespace
{
	// 한 칸 안 나올 때마다 기본 가중치의 이 비율만큼 더 얹는다. 0.5 면 네 칸 연속 없을 때 세 배
	constexpr float SpecialRoomPityGrowth = 0.5f;

	ESectorType RollRoomType(const TArray<float>& Weights)
	{
		float Sum = 0.f;
		for (const float W : Weights)
		{
			Sum += W;
		}

		float Roll = randStream.FRand() * Sum;
		for (int32 i = 0; i < Weights.Num(); i++)
		{
			Roll -= Weights[i];
			if (Weights[i] > 0.f && Roll <= 0.f)
			{
				return static_cast<ESectorType>(i);
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[Route] 굴릴 가중치가 없다 — 몬스터로 할당 (합 %.2f)"), Sum);
		return ESectorType::Monster;
	}
}

void RouteGenSystem::Initialize(const TArray<FSectorWeight>& Weights)
{
	DefaultWeights = Weights;
	
	PreRouteRules.Reset();
	PreRouteRules = {
		MakeShared<MakeRouteStartCommand>(),
		MakeShared<BeforeBossRestCommand>(),
		// MakeShared<MakeRouteStoreCommand>(),
		MakeShared<MakeBossRoomCommand>(),
		MakeShared<MakeStartRoomCommand>(),
		MakeShared<LevelEnvSetup>(),
	};
	
	PostRouteRules.Reset();
	PostRouteRules = {
	};
}

void RouteGenSystem::generateRoute(FVirRoute*& OutVirtualRoute)
{
	//경로 초기화
	OutVirtualRoute->Levels.Reset();
	OutVirtualRoute->Levels.SetNum(OutVirtualRoute->MaxY);
	for (int i = 0; i < OutVirtualRoute->MaxY; i++) {
		OutVirtualRoute->Levels[i].Sectors.SetNum(OutVirtualRoute->MaxX);
	}
	
	//시작 지점 생성
	TArray<int> routeStart;
	int StartCount = randStream.RandRange(2, FMath::Min(OutVirtualRoute->RouteCount, OutVirtualRoute->MaxX-1));	//시작 지점 개수
	routeStart.SetNum(StartCount);
	for (int i = 0; i < StartCount; i++) {
		routeStart[i] = randStream.RandRange(0, OutVirtualRoute->MaxX-1);
		for (int j = 0; j < i; j++) {
			if (routeStart[i] == routeStart[j]) {
				i--;
				break;
			}
		}
	}
	
	//루트 생성 시작
	//1. 뽑힌 시작 지점 전체를 돌면서 1번씩 생성
	for (int i = 0; i < StartCount; i++) {
		makeOneRoute(routeStart[i], OutVirtualRoute);
	}
	
	//2. 남은 루트는 랜덤으로 시작 지점 중 하나를 뽑아 생성
	for (int i = 0; i < OutVirtualRoute->RouteCount - StartCount; i++) {
		makeOneRoute(routeStart[randStream.RandRange(0, StartCount-1)], OutVirtualRoute);
	}
	
	preRulesExecute(OutVirtualRoute);
	
	//생성된 루트에 랜덤 방 할당
	makeRoom(OutVirtualRoute);
	
	postRulesExecute(OutVirtualRoute);
}

void RouteGenSystem::makeOneRoute(int startX, FVirRoute*& OutVirtualRoute) 
{
	int curX = startX;
    
	for (int i = 0; i < OutVirtualRoute->MaxY; i++) 
	{
		// 1. 현재 위치 노드 할당
		OutVirtualRoute->Levels[i].Sectors[curX].Type = ESectorType::Allot;
       
		// 2. 마지막 층(보스 층)이면 더 이상 위로 올라갈 간선을 만들지 않고 종료
		if (i == OutVirtualRoute->MaxY - 1)
		{
			break;
		}
       
		// 랜덤 방향 설정
		constexpr int dirSize = UE_ARRAY_COUNT(Direction);
		int randX = randStream.RandRange(0, dirSize - 1);
       
		for (int j = 0; j < dirSize; j++) 
		{
			int dirIdx = (randX + j) % dirSize;
			int nextX = curX + Direction[dirIdx];
          
			// 범위 벗어나면 스킵
			if (nextX < 0 || nextX >= OutVirtualRoute->MaxX) 
			{
				continue;
			}
          
			// 교차 검증 (i > 0일 때, 이전 층과의 대각선 X자 교차 방지)
			if (OutVirtualRoute->Levels[i].Sectors[nextX].ConnectedSectors.Contains(curX))
			{
				continue; // 교차 발생 시 이 방향은 취소하고 다른 방향 선택
			}
          
			// AddUnique를 사용해 동일한 연결이 중복 추가되는 것을 방지
			OutVirtualRoute->Levels[i].Sectors[curX].ConnectedSectors.AddUnique(nextX);
          
			// 다음 층의 X 좌표로 업데이트하고 방향 탐색 종료
			curX = nextX;
			break;
		}
	}
}

//추후 확률 보정 추가
void RouteGenSystem::makeRoom(FVirRoute*& OutVirtualRoute) {
	// 안 나온 횟수를 들고 있어서 루트마다 새로 만든다. 앞 루트의 누적이 넘어오면 안 된다
	TArray<TUniquePtr<IRoomRule>> RoomRules;
	RoomRules.Add(MakeUnique<FSpecialRoomPityRule>(ESectorType::Store, SpecialRoomPityGrowth));
	RoomRules.Add(MakeUnique<FSpecialRoomPityRule>(ESectorType::Event, SpecialRoomPityGrowth));
	RoomRules.Add(MakeUnique<FSpecialRoomPityRule>(ESectorType::Elite, SpecialRoomPityGrowth));

	const int32 TypeCount = StaticEnum<ESectorType>()->NumEnums() - 1;

	for (int i = 0; i < OutVirtualRoute->MaxY; i++) {
		for (int j = 0; j < OutVirtualRoute->MaxX; j++) {
			FVirSector& Sector = OutVirtualRoute->Levels[i].Sectors[j];
			if (Sector.Type == ESectorType::None) {
				continue;
			}

			if (Sector.Type == ESectorType::Allot) {
				// 칸마다 새로 뜬다. 룰이 고친 값이 다음 칸으로 넘어가지 않는다
				TArray<float> Weights;
				Weights.Init(0.f, TypeCount);
				for (const FSectorWeight& Weight : DefaultWeights) {
					Weights[(int32)Weight.Type] += Weight.Weight;
				}

				for (const TUniquePtr<IRoomRule>& Rule : RoomRules) {
					Rule->Execute(Weights);
				}

				Sector.Type = RollRoomType(Weights);
			}

			// 룰이 미리 박은 방도 나온 것으로 친다. 안 그러면 상점 확정 층 뒤에도 상점 가중치가 계속 오른다
			for (const TUniquePtr<IRoomRule>& Rule : RoomRules) {
				Rule->Result(Sector.Type);
			}
		}
	}
}

void RouteGenSystem::preRulesExecute(FVirRoute*& OutVirtualRoute)
{
	for (const TSharedPtr<IRouteEvent>& Rule : PreRouteRules)
	{
		Rule->Execute(OutVirtualRoute);
	}
}

void RouteGenSystem::postRulesExecute(FVirRoute*& OutVirtualRoute)
{
	for (const TSharedPtr<IRouteEvent>& Rule : PostRouteRules)
	{
		Rule->Execute(OutVirtualRoute);
	}
}