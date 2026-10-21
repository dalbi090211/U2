#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PCGMap/MapBuild/MapStruct.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/SectorPieceList.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/SectorPieceType.h"

#include "SectorPieceSet.generated.h"

// 환경 하나가 쓰는 조각 묶음. SectorPieceSetDir 에 env별로 하나씩 둔다
UCLASS()
class TEAMPROJECT_FIN_API USectorPieceSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Sector")
	EEnvironmentType EnvType = EEnvironmentType::Lab;

	UPROPERTY(EditAnywhere, Category = "Sector")
	TMap<ESectorPieceType, FSectorPieceList> Pieces;
};
