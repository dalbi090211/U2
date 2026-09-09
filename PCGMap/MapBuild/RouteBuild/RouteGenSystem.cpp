#include "RouteGenSystem.h"

#include "TeamProject_Fin/PCGMap/MapBuild/MapStatic.h"

void RouteGenSystem::Initialize(const TArray<FSectorWeight>& Weights)
{
	DefaultWeights = Weights;
	
	PreRouteRules.Reset();
	PreRouteRules = {
		MakeShared<MakeRouteStartCommand>(),
		MakeShared<BeforeBossRestCommand>(),
		MakeShared<MakeRouteStoreCommand>(),
		MakeShared<MakeBossRoomCommand>(),
		MakeShared<MakeStartRoomCommand>(),
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
	float WeightSum = 0.f;
	
	for (const FSectorWeight& Weight : DefaultWeights) {
		WeightSum += Weight.Weight;
	}
	
	for (int i = 0; i < OutVirtualRoute->MaxY; i++) {
		for (int j = 0; j < OutVirtualRoute->MaxX; j++) {
			if (OutVirtualRoute->Levels[i].Sectors[j].Type == ESectorType::Allot) {
				float selectWeight = randStream.RandRange(0.f, WeightSum);
				
				for (const FSectorWeight& Weight : DefaultWeights) {
					selectWeight -= Weight.Weight;
					if (selectWeight <= KINDA_SMALL_NUMBER) {
						OutVirtualRoute->Levels[i].Sectors[j].Type = Weight.Type;
						break;
					}
				}
				
				if (OutVirtualRoute->Levels[i].Sectors[j].Type == ESectorType::Allot) {
					UE_LOG(LogTemp, Warning, TEXT("앱실론보다 큰 오차가 발생, 일단 몬스터로 할당"));
					OutVirtualRoute->Levels[i].Sectors[j].Type = ESectorType::Monster;
				}
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