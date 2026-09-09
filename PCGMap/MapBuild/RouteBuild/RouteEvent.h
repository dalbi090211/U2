#pragma once

#include "TeamProject_Fin/PCGMap/MapBuild/MapStruct.h"

class IRouteEvent {
	public :
		virtual void Execute(FVirRoute*& OutVirtualRoute) = 0;	//구현 강제
		virtual ~IRouteEvent() {}
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
		for (FVirSector& Sector : OutVirtualRoute->Levels[OutVirtualRoute->MaxY / 2].Sectors)
		{
			if (Sector.Type == ESectorType::Allot)	//할당은 allot인 대상만 실행하기에 pre에서 바꿔버리면 할당되지 않음
			{
				Sector.Type = ESectorType::Store;
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