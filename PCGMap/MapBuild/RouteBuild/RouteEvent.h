#pragma once

#include "TeamProject_Fin/PCGMap/MapBuild/MapStruct.h"
#include "TeamProject_Fin/PCGMap/MapBuild/MapStatic.h"

class IRouteEvent {
	public :
		virtual void Execute(FVirRoute*& OutVirtualRoute) = 0;	//구현 강제
		virtual ~IRouteEvent() {}
};


/*
 * 방 하나의 타입을 뽑을 때마다 끼어드는 룰. makeRoom 이 칸마다 부른다.
 * Weights 는 ESectorType 으로 색인한다
 */
class IRoomRule {
public:
	virtual ~IRoomRule() {}

	// 뽑기 직전. 타입별 가중치를 고친다
	virtual void Execute(TArray<float>& Weights) = 0;

	// 그 칸에 무엇이 들어갔는지. 룰이 미리 박아 둔 방도 여기로 온다
	virtual void Result(ESectorType Picked) = 0;
};

// 특수방이 안 나온 칸 수만큼 가중치를 키운다. 나오면 처음으로 되돌린다
class FSpecialRoomPityRule : public IRoomRule {
public:
	FSpecialRoomPityRule(ESectorType InType, float InGrowth) : Type(InType), Growth(InGrowth) {}

	// 곱으로 키워서 기본 가중치가 0 인 타입은 끝까지 0 이다
	virtual void Execute(TArray<float>& Weights) override {
		Weights[(int32)Type] *= 1.f + Misses * Growth;
	}

	virtual void Result(ESectorType Picked) override {
		Misses = Picked == Type ? 0 : Misses + 1;
	}

private:
	ESectorType Type;
	float Growth;
	int32 Misses = 0;
};


//pre ==================================================================================
//첫 방은 반드시 전투여야함
class MakeRouteStartCommand : public IRouteEvent {
public:
	MakeRouteStartCommand() {
	}

	virtual void Execute(FVirRoute*& OutVirtualRoute) override {
		for (FVirSector& Sector : OutVirtualRoute->Levels[0].Sectors)
		{
			if (Sector.Type == ESectorType::Allot)	//할당은 allot인 대상만 실행하기에 pre에서 바꿔버리면 할당되지 않음
			{
				Sector.Type = ESectorType::Monster;
			}
		}
	}
};

//보스 전의 방은 반드시 휴식이어야함
class BeforeBossRestCommand : public IRouteEvent {
public:
	BeforeBossRestCommand() {
	}

	virtual void Execute(FVirRoute*& OutVirtualRoute) override {
		for (FVirSector& Sector : OutVirtualRoute->Levels[OutVirtualRoute->MaxY - 1].Sectors)
		{
			if (Sector.Type == ESectorType::Allot)	//할당은 allot인 대상만 실행하기에 pre에서 바꿔버리면 할당되지 않음
			{
				Sector.Type = ESectorType::Store;
			}
		}
	}
};

//중간 지점에 상점이 존재해야함
class MakeRouteStoreCommand : public IRouteEvent {
public:
	MakeRouteStoreCommand() {
	}

	virtual void Execute(FVirRoute*& OutVirtualRoute) override {
		// 첫 층과 보스 앞 층은 앞 룰이 이미 Allot 을 걷어갔다. 겹치면 상점이 조용히 안 생긴다
		const int32 Middle = OutVirtualRoute->MaxY / 2;
		checkf(Middle > 0 && Middle < OutVirtualRoute->MaxY - 1,
			   TEXT("[Route] 중간 상점 층이 첫 층이나 보스 앞 층과 겹침: MaxY=%d"), OutVirtualRoute->MaxY);

		for (FVirSector& Sector : OutVirtualRoute->Levels[Middle].Sectors)
		{
			if (Sector.Type == ESectorType::Allot)	//할당은 allot인 대상만 실행하기에 pre에서 바꿔버리면 할당되지 않음
			{
				Sector.Type = ESectorType::Store;
			}
		}
	}
};

//루트마다 env 분포를 다르게 굴려 섹터에 박는다. 층이 다 만들어진 뒤여야 하므로 pre 마지막에 둔다
class LevelEnvSetup : public IRouteEvent {
public:
	LevelEnvSetup() {
	}

	virtual void Execute(FVirRoute*& OutVirtualRoute) override {
		Route = OutVirtualRoute;
		if (!Route || Route->Levels.Num() <= 0)
		{
			return;
		}

		EnvRatios.Reset();

		// applyLinear 한 줄이 확률 하나. 같은 env를 두 번 넣으면 그만큼 더해짐
		// 후반부에 어려운 환경이 배치되어야 하는 경우에 양수 a, 반대의 경우 음수 a를 적용
		switch (Route->RouteIndex)
		{
		case 0:
			applyLinear(2, 1, EEnvironmentType::Lab);
			// applyLinear(2, 1, EEnvironmentType::Snow);
			break;

		case 1:
			applyLinear(2, 1, EEnvironmentType::Lab);
			applyLinear(2, 3, EEnvironmentType::Cave);
			break;

		case 2:
			applyLinear(2, 1, EEnvironmentType::Snow);
			applyLinear(2, 3, EEnvironmentType::Cave);
			break;

		default:
			applyLinear(2, 1, EEnvironmentType::Lab);
			break;
		}

		rollSectors();
	}

private:
	FVirRoute* Route = nullptr;

	// 인덱스가 레벨, 값이 그 레벨에서의 가중치
	TArray<TPair<EEnvironmentType, TArray<int32>>> EnvRatios;

	// 층 L의 가중치를 x*L + y 로 채운다. 기울기가 음수여도 0 밑으로는 안 내려간다
	void applyLinear(int32 x, int32 y, EEnvironmentType Env) {
		TArray<int32> Ratio;
		Ratio.SetNum(Route->Levels.Num());

		for (int32 L = 0; L < Ratio.Num(); L++)
		{
			Ratio[L] = FMath::Max(0, x * L + y);
		}

		EnvRatios.Emplace(Env, MoveTemp(Ratio));
	}

	// 층마다 가중치 합으로 굴려 그 층의 섹터들에 env를 채운다
	void rollSectors() {
		for (int32 Y = 0; Y < Route->Levels.Num(); Y++)
		{
			int32 Sum = 0;
			for (const TPair<EEnvironmentType, TArray<int32>>& Pair : EnvRatios)
			{
				Sum += Pair.Value[Y];
			}

			for (FVirSector& Sector : Route->Levels[Y].Sectors)
			{
				// 그 층의 확률이 전부 0이면 굴릴 것이 없다
				if (Sum <= 0)
				{
					Sector.EnvType = EnvRatios.Num() > 0 ? EnvRatios[0].Key : EEnvironmentType::Lab;
					continue;
				}

				int32 Roll = randStream.RandRange(0, Sum - 1);
				for (const TPair<EEnvironmentType, TArray<int32>>& Pair : EnvRatios)
				{
					Roll -= Pair.Value[Y];
					if (Roll < 0)
					{
						Sector.EnvType = Pair.Key;
						break;
					}
				}
			}
		}
	}
};

//post =================================================================================

class MakeBossRoomCommand : public IRouteEvent {
public:
	MakeBossRoomCommand() {
	}

	virtual void Execute(FVirRoute*& OutVirtualRoute) override {
		// 최상단 위에 보스 전용 층(1칸)을 추가
		int32 BossLevelIndex = OutVirtualRoute->Levels.Num();
		OutVirtualRoute->Levels.SetNum(BossLevelIndex + 1);
		OutVirtualRoute->Levels[BossLevelIndex].Sectors.SetNum(OutVirtualRoute->MaxX);
		OutVirtualRoute->Levels[BossLevelIndex].Sectors[OutVirtualRoute->MaxX/2].Type = ESectorType::Boss;
       
		// 기존 최상단 층을 전부 보스방으로 연결
		int32 TopFloorIndex = BossLevelIndex - 1;
		for (int32 X = 0; X < OutVirtualRoute->Levels[TopFloorIndex].Sectors.Num(); X++)
		{
			FVirSector& TopSector = OutVirtualRoute->Levels[TopFloorIndex].Sectors[X];
          
			if (TopSector.Type != ESectorType::None)
			{
				TopSector.ConnectedSectors.AddUnique(OutVirtualRoute->MaxX/2); // 보스 방(0번 슬롯)으로 연결
			}
		}
	}
};

class MakeStartRoomCommand : public IRouteEvent {
public:
	MakeStartRoomCommand() {
	}

	virtual void Execute(FVirRoute*& OutVirtualRoute) override {
		
	}
};