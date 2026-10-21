#pragma once

#include "CoreMinimal.h"
#include "RouteBuild/RouteGenSystem.h"
#include "SectorLevelSet.h"
#include "Engine/LevelStreamingDynamic.h"
#include "UI/RouteWidget.h"
#include "TimerManager.h"
#include "WorldGenSubSystem.generated.h"

class ULevelStreamingDynamic;
class AJumpMapLayoutController;
class ABasePlayerController;

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

	// 받은 시드와 시트 값으로 루트를 만든다. 같은 시드로 이미 만들었으면 아무것도 하지 않는다.
	void EnsureRoute(int32 Seed, const FWorldGenSheet& InSheet);

	// 서버 전용. GetRunSeed 뒤에 유효하다
	const FWorldGenSheet& GetSheet() const { return Sheet; }

	// 서버 전용. 검증 후 다음 섹터로 이동한다.
	void ChangeLevel(FIntPoint pos);

	/*
	 * 서버 전용. 지금 있는 노드를 그대로 다시 연다. 루트 진행과 시드는 건드리지 않는다.
	 * 조립 방은 같은 배치를 다시 세우고, 통짜 레벨은 내렸다가 새 인스턴스로 올린다
	 */
	void RetryCurrentSector();

	/*
	 * 서버 전용. 이번 런을 버린다. 다음에 시드를 물으면 새 루트가 나온다.
	 * 맵 전환에 딸려 자동으로 도는 것이 아니라, 포기를 고른 자리에서만 부른다
	 */
	void AbandonRun();

	// 파티 복귀 시 서버/클라이언트 모두 이전 런 캐시를 비운다.
	void ResetRunForLobby();

	// 서버 전용. 지금 위치에서 갈 수 있는 노드의 레벨을 올려두고 숨긴다
	void PreloadReachableSectors();

	/*
	 * 서버/클라 공통. 해당 노드의 섹터를 켠다.
	 * 서버는 프리로드해 둔 핸들을 쓰고, 클라는 여기서 새로 올린다.
	 * 인스턴스 이름을 노드 좌표로 고정해야 안에 배치된 액터가 양쪽에서 같은 이름을 갖는다.
	 */
	void ApplySector(TSoftObjectPtr<UWorld> Level, FIntPoint Pos, int32 Retry = 0);

	// 도착 공통 처리. 각 머신이 자기 화면을 걷고, 서버만 폰을 옮긴다
	void FinishTransition(const FTransform& Entry);

	// PC의 준비 보고/완료 통보를 현재 점프맵 인스턴스에만 연결한다.
	void ReportJumpMapReady(ABasePlayerController* Player, AJumpMapLayoutController* LayoutController, bool bSuccess);
	void CompleteJumpMapTransition(AJumpMapLayoutController* LayoutController);

	// 서버/클라 공통. 직전에 연 것이 통짜 서브 레벨이면 내린다
	void UnloadLastSubLevel();

	// 서버 전용. env에 맞는 조각으로 방 하나를 조립해 GameState에 올린다
	void BuildSector(EEnvironmentType Env, FIntPoint Pos);

	// env/타입에 등록된 레벨 목록. 없으면 널
	const TArray<TSoftObjectPtr<UWorld>>* FindSectorLevels(EEnvironmentType Env, ESectorType Type) const;

	// curPos 에서 pos 로 갈 수 있는가
	bool CanMoveTo(FIntPoint pos);
	bool IsSectorChanging() const { return bSectorChanging; }

	// 위젯은 ABasePlayerController 가 소유한다. 여기는 데이터만 들고 있는다.
	FVirRoute *GetRoute() { return bRouteReady && virtualRoutes.IsValidIndex(RouteIndex) ? &virtualRoutes[RouteIndex] : nullptr; }
	FIntPoint GetCurPos() const { return curPos; }

	// 서버 전용. 지금까지 고른 노드들. 클라는 Client_OpenRouteMap 인자로 통째로 받는다
	const TArray<FIntPoint>& GetVisited() const { return visitedPath; }

private:
	RouteGenSystem routeMaker;

	// SectorLevelSetDir 에서 env별로 하나씩 읽어둔다
	UPROPERTY()
	TMap<EEnvironmentType, TObjectPtr<USectorLevelSet>> EnvLevelSets;

	void LoadEnvLevelSets();

	// 서버는 런을 시작할 때 시트에서 채운다. 클라는 EnsureRoute 로 받은 값
	FWorldGenSheet Sheet;

	TArray<FVirRoute> virtualRoutes = { FVirRoute(), FVirRoute(), FVirRoute() };

	// 미리 올려둔 섹터. 키는 루트 좌표
	UPROPERTY()
	TMap<FIntPoint, FPreloadedSector> PreloadedSectors;

	// 페이드가 도는 동안 두 번째 선택을 막는다
	bool bSectorChanging = false;

	// 지금 보이는 섹터와, 페이드가 끝나고 켜는 중인 섹터
	UPROPERTY()
	TObjectPtr<ULevelStreamingDynamic> CurrentSector;

	UPROPERTY()
	TObjectPtr<ULevelStreamingDynamic> PendingSector;

	bool bPendingJumpMap = false;
	bool bJumpMapTransitionFailed = false;
	TWeakObjectPtr<AJumpMapLayoutController> AwaitedJumpMapController;
	TWeakObjectPtr<UWorld> JumpMapWorld;
	FTransform JumpMapEntry = FTransform::Identity;
	FTimerHandle JumpMapTimeoutHandle;

	bool IsJumpMap(const TSoftObjectPtr<UWorld>& Level) const;
	void OnJumpMapLayoutFinished(AJumpMapLayoutController* LayoutController, bool bSuccess);
	void OnJumpMapParticipantsReady(AJumpMapLayoutController* LayoutController, bool bSuccess);
	void FailJumpMapTransition(const FString& Reason);
	void ClearJumpMapPreparation();

	// CurrentSector 에 통짜 서브 레벨이 등록돼 있다
	bool isLastSubLevel = false;

	// 페이드가 다 내려간 뒤 PendingSector 를 켠ㅋ
	void ShowPendingSector();

	// 켜진 섹터가 보이기 시작하면 폰을 옮기고 이전 섹터를 내린다
	UFUNCTION()
	void OnSectorShown();

	int RouteIndex = 0;
	bool isDebug = true;
	FIntPoint curPos = FIntPoint(0, -1);
	TArray<FIntPoint> visitedPath;

	int32 RunSeed = 0;
	bool bRouteReady = false;

	void SetupVars();

	// 페이드를 깔고 시간이 지나면 그 노드를 연다. ChangeLevel 과 RetryCurrentSector 가 나눠 쓴다
	void EnterSector(FIntPoint Pos, int32 Retry);

	// 이 런에서 같은 노드를 몇 번째로 여는지. 스트리밍 인스턴스 이름이 겹치는 것을 막는다
	int32 RetryCount = 0;

	// 재도전 때 다시 올릴 레벨. 프리로드가 소진된 뒤에는 여기밖에 남는 곳이 없다
	UPROPERTY()
	TSoftObjectPtr<UWorld> LastSectorLevel;

	/*
	 * 월드가 갈리면 여기 담긴 스트리밍 객체는 같이 죽는다. 서브시스템은 게임 인스턴스에 살아서
	 * 그대로 들고 있게 되고, 다음 선택이 죽은 포인터를 보고 조용히 막힌다
	 */
	void OnPreLoadMap(const FString& MapName);

	// 새 월드가 섰으니 갈 수 있는 곳을 다시 올린다
	void OnPostLoadMap(UWorld* LoadedWorld);
};
