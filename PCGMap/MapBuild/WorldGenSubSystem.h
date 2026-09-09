#pragma once

#include "CoreMinimal.h"
#include "RouteBuild/RouteGenSystem.h"
#include "SectorBuild/SectorGenSystem.h"
#include "UI/RouteWidget.h"
#include "WorldGenSubSystem.generated.h"

UCLASS()
class TEAMPROJECT_FIN_API UWorldGenSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TSubclassOf<URouteWidget> RouteWidgetClass;

	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	virtual void Deinitialize() override;

	/*
	 * 서버 전용. 이번 런의 시드를 반환하고, 아직 없으면 여기서 뽑아 루트를 만든다.
	 * 클라는 이 값을 Client_OpenRouteMap 인자로 받아 EnsureRoute 에 넣는다.
	 */
	int32 GetRunSeed();

	// 받은 시드로 루트를 만든다. 같은 시드로 이미 만들었으면 아무것도 하지 않는다.
	void EnsureRoute(int32 Seed);

	// 서버 전용. 검증 후 다음 섹터로 이동한다.
	void ChangeLevel(FIntPoint pos);

	// curPos 에서 pos 로 갈 수 있는가
	bool CanMoveTo(FIntPoint pos) const;

	// 위젯은 ABasePlayerController 가 소유한다. 여기는 데이터만 들고 있는다.
	FVirRoute *GetRoute() const { return bRouteReady && virtualRoutes.IsValidIndex(RouteIndex) ? virtualRoutes[RouteIndex] : nullptr; }
	FIntPoint GetCurPos() const { return curPos; }

	// 서버 전용. 지금까지 고른 노드들. 클라는 Client_OpenRouteMap 인자로 통째로 받는다
	const TArray<FIntPoint>& GetVisited() const { return visitedPath; }

private:
	SectorGenSystem sectorMaker;
	RouteGenSystem routeMaker;

	static constexpr int32 RouteCount = 6; // 생성할 경로 수
	static constexpr int32 RouteMaxX = 7;  // 루트 폭
	// static constexpr int32 RouteMaxY = 14; // 루트 높이(보스층 제외)
	static constexpr int32 RouteMaxY = 4; // 프로토타입용

	TArray<FVirRoute *> virtualRoutes = {
		new FVirRoute(EEnvironmentType::Lab, RouteCount, RouteMaxX, RouteMaxY),
		new FVirRoute(EEnvironmentType::Lab, RouteCount, RouteMaxX, RouteMaxY),
		new FVirRoute(EEnvironmentType::Lab, RouteCount, RouteMaxX, RouteMaxY),
	};

	int RouteIndex = 0;
	bool isDebug = true;
	FIntPoint curPos = FIntPoint(0, -1);
	TArray<FIntPoint> visitedPath;

	int32 RunSeed = 0;
	bool bRouteReady = false;

	// 트래블을 시작한 월드. 이 월드에서 두 번째 선택이 오면 무시한다.
	// 트래블 후에는 월드가 바뀌므로 별도 리셋 없이 저절로 풀린다.
	TWeakObjectPtr<UWorld> TravelStartedIn;

	void SetupVars();
	void DebugRoute();
};
