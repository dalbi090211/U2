#pragma once

#include "CoreMinimal.h"
#include "SectorBuild/Sector.h"
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
	Lab
};

USTRUCT(BlueprintType)
struct FGenNodeList
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere)
	TArray<TSoftClassPtr<ASector>> Nodes;
};

USTRUCT(BlueprintType)
struct FGenLevelList
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere)
	TArray<FName> LevelNames;
};

USTRUCT(BlueprintType)
struct FSectorWeight
{
	GENERATED_BODY()

public :
	// 0 이상: 앞에서부터 인덱스 / 음수: 뒤에서부터 (-1 = 마지막 레벨)
	UPROPERTY(EditAnywhere)
	int32 Level = 0;

	UPROPERTY(EditAnywhere)
	ESectorType Type = ESectorType::None;

	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};

//현재는 type밖에 없지만 추후에 날씨, 특수이벤트 등을 위해 만듬
USTRUCT(BlueprintType)
struct FVirSector
{
	GENERATED_BODY()
	
public :
	UPROPERTY(EditAnywhere)
	ESectorType Type = ESectorType::None;
	
	TArray<int> ConnectedSectors;
	bool bVisited = false;
};

USTRUCT(BlueprintType)
struct FVirLevel
{
	GENERATED_BODY()

public :
	TArray<FVirSector> Sectors;
};

class FVirRoute
{
	public :
		FVirRoute(EEnvironmentType type, int RouteCount, int MaxX, int MaxY) 
			: DefaultEnvType(type), RouteCount(RouteCount), MaxX(MaxX), MaxY(MaxY)
		{
		}
		
		EEnvironmentType DefaultEnvType;
		int RouteCount;	//생성할 루트의 개수, 루트의 시작지점은 같을 수 있음
		int MaxX, MaxY;	//루트의 x, y 최대 값
		
		TArray<FVirLevel> Levels;
};
