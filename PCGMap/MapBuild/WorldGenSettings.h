#pragma once

#include "CoreMinimal.h"
#include "MapStruct.h"
#include "Engine/DeveloperSettings.h"

#include "WorldGenSettings.generated.h"

class URouteWidget;
class UWorld;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "World Gen"))
class TEAMPROJECT_FIN_API UWorldGenSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// UPROPERTY(Config, EditAnywhere, Category = "Sector")
	// TSoftObjectPtr<UDataTable> SectorTable;

	UPROPERTY(Config, EditAnywhere, Category = "Route")
	TSoftClassPtr<URouteWidget> RouteWidgetClass;
	
	UPROPERTY(Config, EditAnywhere, Category = "Route")
	bool isDebug = true;
	
	UPROPERTY(Config, EditAnywhere, Category = "Generation")
	int32 Seed = 0;
	
	UPROPERTY(Config, EditAnywhere, Category = "Generation")
	float SectorInterval = 1000.f;

	// 원본 레벨 참조로 점프맵을 판정한다. 비교할 때 에셋을 로드하지 않는다.
	UPROPERTY(Config, EditAnywhere, Category = "Jump Map")
	TArray<TSoftObjectPtr<UWorld>> JumpMapLevels;
	
	// 레벨 지정이 없는 구간에 적용
	UPROPERTY(Config, EditAnywhere, Category = "Spawn Rate")
	TArray<FSectorWeight> DefaultWeights;

	// 구글 시트 탭의 CSV 주소. 열: Name, Value
	UPROPERTY(Config, EditAnywhere, Category = "Sheet")
	FString ValuesSheetUrl;

	// 구글 시트 탭의 CSV 주소. 첫 줄: 방 타입 이름들(Monster, Event ...), 둘째 줄: 가중치
	UPROPERTY(Config, EditAnywhere, Category = "Sheet")
	FString WeightsSheetUrl;
};
