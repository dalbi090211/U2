#pragma once

#include "CoreMinimal.h"
#include "MapStruct.h"
#include "Engine/DeveloperSettings.h"

#include "WorldGenSettings.generated.h"

class URouteWidget;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "World Gen"))
class TEAMPROJECT_FIN_API UWorldGenSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// UPROPERTY(Config, EditAnywhere, Category = "Sector")
	// TSoftObjectPtr<UDataTable> SectorTable;

	UPROPERTY(Config, EditAnywhere, Category = "Sector")
	TMap<ESectorType, FGenNodeList> GenNodes;
	
	UPROPERTY(Config, EditAnywhere, Category = "Sector")
	TMap<ESectorType, FGenLevelList> GenLevelNames;

	UPROPERTY(Config, EditAnywhere, Category = "Route")
	TSoftClassPtr<URouteWidget> RouteWidgetClass;
	
	UPROPERTY(Config, EditAnywhere, Category = "Route")
	bool isDebug = true;
	
	UPROPERTY(Config, EditAnywhere, Category = "Generation")
	int32 Seed = 0;
	
	UPROPERTY(Config, EditAnywhere, Category = "Generation")
	float SectorInterval = 1000.f;
	
	// 레벨 지정이 없는 구간에 적용
	UPROPERTY(Config, EditAnywhere, Category = "Spawn Rate")
	TArray<FSectorWeight> DefaultWeights;
};
