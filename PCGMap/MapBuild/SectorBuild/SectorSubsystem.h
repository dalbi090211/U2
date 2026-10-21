#pragma once

#include "CoreMinimal.h"
#include "PCGMap/MapBuild/MapStruct.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/DoorShapeType.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/SectorPieceSet.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/SectorRule.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SectorSubsystem.generated.h"

/*
 * 조각에 붙은 문 하나. Transform 은 문 액터 피벗이 아니라 두 문이 맞닿는 접점 — 문 메시 앞면의 한가운데.
 * 피벗은 문틀 한쪽 끝에 있어서 피벗끼리 맞대면 문 폭만큼 옆으로 어긋난다.
 * 조각 로컬, 열린 문을 담을 때만 월드. 스케일은 항상 1
 */
USTRUCT()
struct FSectorDoor
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	// 이 값이 같은 문끼리만 이어붙인다. None 이면 아무 문에나 붙는다(시작 조각)
	UPROPERTY()
	EDoorShapeType DoorType = EDoorShapeType::None;

	bool FitsInto(const FSectorDoor& Other) const
	{
		return Other.DoorType == EDoorShapeType::None || DoorType == Other.DoorType;
	}
};

// 조각 하나를 프로브로 한 번 재둔 결과. 전부 액터 로컬 좌표
USTRUCT()
struct FSectorShape
{
	GENERATED_BODY()

	// 어느 조각인지. 후보로 넘길 때 이걸로 스폰한다
	UPROPERTY()
	TSubclassOf<ASector> Piece;

	// 조각 묶음에서 이 조각이 달려 있던 칸. 룰이 이것만 보고 판단한다
	UPROPERTY()
	ESectorPieceType Type = ESectorPieceType::Start;

	// 이 조각에 붙은 문 전부. 하나는 들어오는 데 쓰고 나머지가 나가는 문이 된다
	UPROPERTY()
	TArray<FSectorDoor> Doors;

	// 겹침 판정용. 조각 로컬 AABB — 놓인 자리와 짝지어 비교하므로 월드로 누르지 않는다
	UPROPERTY()
	TArray<FBox> Volumes;

	bool IsValid() const { return Doors.Num() > 0; }

	// None 은 아무 문에나 붙는다 — 첫 조각이 쓴다
	bool HasDoor(EDoorShapeType DoorType) const
	{
		return DoorType == EDoorShapeType::None
			|| Doors.ContainsByPredicate([DoorType](const FSectorDoor& Door)
			   {
				   return Door.DoorType == DoorType;
			   });
	}
};

/*
 * 이미 놓인 조각의 볼륨 하나. 로컬 상자와 그 조각이 선 자리를 따로 든다.
 * 미리 월드 AABB 로 눌러 두면 비스듬히 놓인 조각이 제 크기보다 부푼 채로 남는다 —
 * 45도로 놓인 1000x400 복도는 990x990 짜리 상자가 되어 옆자리를 통째로 막는다
 */
struct FSectorVolume
{
	FSectorVolume() = default;
	FSectorVolume(const FBox& InLocal, const FTransform& InFrame) : Local(InLocal), Frame(InFrame) {}

	FBox Local = FBox(ForceInit);
	FTransform Frame;
};

/*
 * 놓인 조각들이 문으로 어떻게 이어졌는지. 조립이 끝난 뒤 문 접점을 맞춰 만든다.
 * 재귀를 따라가지 않고 기하로 만들기 때문에, 우연히 맞닿아 생긴 고리까지 그대로 들어온다
 */
struct FSectorGraph
{
	TArray<TArray<int32>> Adjacent;

	// 시작 조각에서 몇 칸 떨어졌나. 못 닿는 조각은 INDEX_NONE
	TArray<int32> Depth;

	// 시작 쪽으로 한 칸 되돌아가는 조각
	TArray<int32> Parent;

	// 짝이 없는 문 — (조각 번호, 그 조각의 문 번호)
	TArray<TPair<int32, int32>> FreeDoors;
};

// 조립 한 번 동안의 상태
struct FSectorBuildContext
{
	//생성용 고정값
	UWorld* World = nullptr;
	USectorPieceSet* PieceSet = nullptr;
	int32 MaxRouteLength = 0;
	int32 EndSectorCount = 0;

	// 자리마다 전부 실행된다. 방 타입 enum 이 생기면 그 enum 이 여기를 채운다
	TArray<TUniquePtr<FSectorRule>> Rules;
	
	//계산용 변동값
	// 이미 찬 자리. 조각을 확정할 때마다 쌓인다
	TArray<FSectorVolume> Occupied;

	int32 TotalCount = 0;
	int32 Budget = 0;
};

// 서버가 조각을 다 세우고 문까지 맞춘 직후. 조각 안의 액터는 이미 BeginPlay 를 마쳤다
DECLARE_MULTICAST_DELEGATE(FOnSectorReady);

UCLASS()
class TEAMPROJECT_FIN_API USectorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// 서버 전용. 스폰 시스템이 이걸 받고 몹을 미리 만든다. 로딩 화면이 걷히기 전에 온다
	FOnSectorReady OnSectorReady;

	// 서버 전용. 실패하면 빈 배열
	TArray<FSectorPlacement> BuildSectors(UWorld* World, EEnvironmentType Env, const FTransform& Origin,
										  const FWorldGenSheet& Sheet);

	// 서버가 받은 배치대로 스폰한다. 클라는 복제로 받으므로 개수만 기억해 둔다
	void SpawnPlacements(UWorld* World, const TArray<FSectorPlacement>& Placements);

	// 클라가 복제로 받은 조각을 알려 온다. 다 모이면 문을 연다
	void RegisterReplicatedSector(ASector* Sector);

	// 조립된 방의 시작 지점. SpawnPlacements 뒤에 유효하다
	FTransform GetEntryTransform() const { return EntryTransform; }

	// 조립해 둔 조각을 전부 지운다. 통짜 레벨로 넘어갈 때도 불러야 한다 —
	// 안 그러면 전투 맵이 상점 레벨과 같은 자리에 겹친 채 남는다
	void ClearSpawned();

private:
	// SectorPieceSetDir 에서 env별로 하나씩
	UPROPERTY()
	TMap<EEnvironmentType, TObjectPtr<USectorPieceSet>> PieceSets;

	// 클래스별 정보 캐시. 처음 필요할 때 프로브를 띄워 재본다
	UPROPERTY()
	TMap<TSubclassOf<ASector>, FSectorShape> ShapeCache;

	UPROPERTY()
	TArray<TObjectPtr<ASector>> SpawnedSectors;

	FTransform EntryTransform = FTransform::Identity;

	void LoadPieceSets();


	// 스폰한 섹터들에서 서로 맞닿은 문을 찾아 열어 둔다
	void openConnectedDoors();

	// 클라가 기다리는 조각 수. 복제는 하나씩 도착하므로 다 모이기 전에는 문을 못 맞춘다
	int32 ExpectedSectorCount = 0;

	// 다 왔으면 문을 연다. 조각과 배치 목록 중 뭐가 먼저 오든 양쪽에서 부른다
	void openDoorsWhenAllArrived();

	// 시작 조각 안에서 플레이어가 설 자리. 액터 피벗은 벽 밖/바닥 밑일 수 있어 그대로 못 쓴다
	static FTransform MakeEntryTransform(AActor* StartPiece);

	/*
	 * BP 가 SCS 로 붙인 문/메시는 CDO 에 없다. 캐시에 없으면 월드에 잠깐 띄워 재고 지운다.
	 * 돌려준 포인터는 다음 EnsureShape 호출까지만 유효하다 — ShapeCache 가 재해시되면 옮겨간다
	 */
	const FSectorShape* EnsureShape(UWorld* World, TSubclassOf<ASector> SectorClass,
									ESectorPieceType Type);

	// 프로브에서 읽은 문 하나를 조각 로컬 접점으로 바꾼다. SectorFrame 은 프로브의 위치·회전(스케일 1)
	static FSectorDoor makeDoor(const FSectorDoorSlot& Slot, const FTransform& SectorFrame);

	// 놓인 조각이 이미 찬 자리와 겹치는가. 직전 조각도 검사한다 —
	// 문 앞면끼리 맞대므로 이음매에서 조각 볼륨은 겹치지 않고 오탐이 없다
	static bool OverlapsOccupied(const FSectorShape& Shape, const FTransform& Placement,
								 const TArray<FSectorVolume>& Occupied);

	//BuildSectors용 초기화 함수
	void setPieceSet(USectorPieceSet*& set, EEnvironmentType Env);
	static void setPieceRules(TArray<TUniquePtr<FSectorRule>>& Rules);

	// DoorType 에 이을 수 있는 조각을 점수 높은 순으로. 룰이 0 으로 깎은 것은 빠진다
	void EvaluateCandidates(FSectorBuildContext& Ctx, EDoorShapeType DoorType,
							const TOptional<ESectorPieceType>& PrevType,
							TArray<FSectorShape>& Out);

	// 붙일 자리만 찾는다. Result 는 안 건드린다 — 확정은 호출부가 한다
	bool tryPlace(const FSectorShape& PlaceSector, const FSectorDoor& OpenDoor,
				  const FSectorBuildContext& Ctx, int32& OutEntryDoor, FTransform& OutTransform) const;

	// 찾은 자리를 확정한다. Result 와 Occupied 에 넣고 남은 자리 수를 줄인다
	void commitPlace(FSectorBuildContext& Ctx, const FSectorShape& Shape,
					 const FTransform& Placed, TArray<FSectorPlacement>& Result);

	// 들어온 문을 뺀 나머지 문마다 다시 내려간다
	void branchFromDoors(FSectorBuildContext& Ctx, const FSectorShape& Shape, const FTransform& Placed,
						 int32 Depth,
						 int32 EntryDoor, TArray<FSectorPlacement>& Result);

	// OpenDoor 에 조각 하나를 붙이고, 그 조각의 문마다 다시 내려간다
	void RecurBuildSector(FSectorBuildContext& Ctx, const FSectorDoor& OpenDoor,
						  const TOptional<ESectorPieceType>& PrevType, int32 Depth,
						  TArray<FSectorPlacement>& Result);

	// 루트 하나를 통째로 짓는다. 끝 방까지 붙여서 돌려준다
	TArray<FSectorPlacement> buildOneRoute(UWorld* World, EEnvironmentType Env, const FTransform& Origin,
										   const FWorldGenSheet& Sheet);

	// 룰을 안 태우고 한 타입만 모은다. 끝 방처럼 자리를 정해 놓고 붙일 때 쓴다
	void gatherPieces(FSectorBuildContext& Ctx, ESectorPieceType Type,
					  EDoorShapeType DoorType, TArray<FSectorShape>& Out);

	// 시작에서 먼 쪽부터 끝 방을 붙인다. 최소 하나는 붙을 때까지 물러서며 다시 시도한다
	void attachEndSectors(FSectorBuildContext& Ctx, TArray<FSectorPlacement>& Result);

	// 이 조각의 빈 문 중 바깥을 보는 쪽부터 끝 방을 대 본다
	bool tryAttachEnd(FSectorBuildContext& Ctx, int32 NodeIndex,
					  const FSectorGraph& Graph, TArray<FSectorPlacement>& Result);

	void makeGraph(const TArray<FSectorPlacement>& Placements, FSectorGraph& Out) const;

	const FSectorShape* findShape(const FSectorPlacement& Placement) const;

	// 높을수록 좋은 루트. 항이 서로 묻히지 않게 가중치는 파일 맨 위에 모아 두었다
	float scoreRoute(const TArray<FSectorPlacement>& Placements) const;
};
