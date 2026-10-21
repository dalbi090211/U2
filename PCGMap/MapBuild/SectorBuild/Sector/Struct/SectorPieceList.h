#pragma once

#include "CoreMinimal.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/Sector.h"

#include "SectorPieceList.generated.h"

// 한 타입에 쓸 수 있는 조각 후보들
USTRUCT(BlueprintType)
struct FSectorPieceList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<TSoftClassPtr<ASector>> Pieces;
};
