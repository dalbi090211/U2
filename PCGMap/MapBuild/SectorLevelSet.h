#pragma once

#include "CoreMinimal.h"
#include "MapStruct.h"
#include "Engine/DataAsset.h"

#include "SectorLevelSet.generated.h"

/*
 * env 하나당 에셋 하나. 파일 이름은 DA_Env_<EEnvironmentType 이름> 으로 맞춘다.
 * UWorldGenSubSystem 이 이 규칙으로 경로를 조립해 로드한다.
 */
inline const FString SectorLevelSetDir = TEXT("/Game/TPSDevelop/Data/SectorLevelSet");
inline const FString SectorLevelSetPrefix = TEXT("DA_Env_");

USTRUCT(BlueprintType)
struct FSectorLevelList
{
	GENERATED_BODY()

	// 소프트 참조라 경로만 들고 있는다. 실제 로드는 스트리밍이 건다
	UPROPERTY(EditAnywhere)
	TArray<TSoftObjectPtr<UWorld>> Levels;
};

UCLASS()
class TEAMPROJECT_FIN_API USectorLevelSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Sector")
	EEnvironmentType EnvType = EEnvironmentType::Lab;

	UPROPERTY(EditAnywhere, Category = "Sector")
	TMap<ESectorType, FSectorLevelList> Levels;

	// 해당 타입에 등록된 레벨 중 하나. 없으면 널
	const TArray<TSoftObjectPtr<UWorld>>* Find(ESectorType Type) const
	{
		const FSectorLevelList* List = Levels.Find(Type);
		return List && List->Levels.Num() > 0 ? &List->Levels : nullptr;
	}
};
