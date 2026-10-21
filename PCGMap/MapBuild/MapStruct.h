#pragma once

#include "CoreMinimal.h"
#include "SectorBuild/Sector/Struct/Sector.h"
#include "MapStruct.generated.h"

UENUM(BlueprintType)
enum class ENodeType : uint8
{
	Start        UMETA(DisplayName = "시작"),
	Connector    UMETA(DisplayName = "연결"),
	Intermediate UMETA(DisplayName = "중간"),
	Objectives   UMETA(DisplayName = "목표"),
	End          UMETA(DisplayName = "종료")
};

UENUM(BlueprintType)
enum class ESectorType : uint8
{
	None	UMETA(DisplayName = "없음"),
	Allot	UMETA(DisplayName = "할당대기"),
	Monster   UMETA(DisplayName = "몬스터"),
	Event UMETA(DisplayName = "랜덤"),
	Elite UMETA(DisplayName = "정예 몬스터"),
	Store  UMETA(DisplayName = "상점"),
	Boss   UMETA(DisplayName = "보스")
};

UENUM(BlueprintType)
enum class EEnvironmentType : uint8
{
	Lab		UMETA(DisplayName = "연구소"),
	Snow	UMETA(DisplayName = "설산"),
	Cave	UMETA(DisplayName = "동굴")
};

USTRUCT(BlueprintType)
struct FSectorWeight
{
	GENERATED_BODY()

public :
	UPROPERTY(EditAnywhere)
	ESectorType Type = ESectorType::None;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};

// 구글 시트에서 읽는 기획 수치. 서버가 런마다 읽고, 클라는 시드와 함께 받는다
// int 필드 이름이 수치 탭 Name 열에 그대로 쓰인다
USTRUCT()
struct FWorldGenSheet
{
	GENERATED_BODY()

	UPROPERTY()
	int32 RouteCount = 0;	// 생성할 경로 수

	UPROPERTY()
	int32 RouteMaxX = 0;	// 루트 폭

	UPROPERTY()
	int32 RouteMaxY = 0;	// 루트 높이(보스층 제외)

	UPROPERTY()
	int32 SectorPieceCount = 0;	// 전투 방 하나의 조각 수. 끝 방 포함

	UPROPERTY()
	int32 EndSectorCount = 0;	// 붙여 볼 끝 방 수. 자리가 없으면 줄지만 하나는 반드시 붙인다

	UPROPERTY()
	int32 MaxRouteLength = 0;	// 한 갈래가 자라는 최대 칸 수

	UPROPERTY()
	TArray<FSectorWeight> Weights;
};

//조립 결과 한 조각. 서버가 정하고 복제로 넘어간다. 액터 자체는 각 머신이 스폰한다
USTRUCT()
struct FSectorPlacement
{
	GENERATED_BODY()

	// 섹터 조각이거나 이음매에 세우는 문
	UPROPERTY()
	TSoftClassPtr<AActor> SpawnClass;

	UPROPERTY()
	FTransform Transform;
};

//프리로드해 둔 섹터 하나. 레벨 에셋은 복제로 클라에 넘겨야 해서 핸들과 같이 들고 있는다
USTRUCT()
struct FPreloadedSector
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<class ULevelStreamingDynamic> Streaming;

	UPROPERTY()
	TSoftObjectPtr<UWorld> Level;
};

//현재는 type밖에 없지만 추후에 날씨, 특수이벤트 등을 위해 만듬
USTRUCT(BlueprintType)
struct FVirSector
{
	GENERATED_BODY()
	
public :
	UPROPERTY(EditAnywhere)
	ESectorType Type = ESectorType::None;

	//LevelEnvSetup이 레벨별 확률로 굴려 채운다
	UPROPERTY(EditAnywhere)
	EEnvironmentType EnvType = EEnvironmentType::Lab;
	
	UPROPERTY()
	TArray<int32> ConnectedSectors;

	UPROPERTY()
	bool bVisited = false;
};

USTRUCT(BlueprintType)
struct FVirLevel
{
	GENERATED_BODY()

public :
	UPROPERTY()
	TArray<FVirSector> Sectors;
};

USTRUCT(BlueprintType)
struct FVirRoute
{
	GENERATED_BODY()

	public :
		FVirRoute() {}

		FVirRoute(int32 RouteIndex, int32 RouteCount, int32 MaxX, int32 MaxY)
			: RouteIndex(RouteIndex), RouteCount(RouteCount), MaxX(MaxX), MaxY(MaxY)
		{
		}

		UPROPERTY()
		int32 RouteIndex = 0;	//몇 번째 루트인지. LevelEnvSetup이 이걸로 분포를 고른다

		UPROPERTY()
		int32 RouteCount = 0;	//생성할 루트의 개수, 루트의 시작지점은 같을 수 있음

		UPROPERTY()
		int32 MaxX = 0;

		UPROPERTY()
		int32 MaxY = 0;	//루트의 x, y 최대 값

		UPROPERTY()
		TArray<FVirLevel> Levels;
};
