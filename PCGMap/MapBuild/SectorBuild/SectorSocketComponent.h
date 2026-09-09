#pragma once

#include "CoreMinimal.h"
#include "Components/ArrowComponent.h"

#include "SectorSocketComponent.generated.h"

UENUM()
enum class ESocketSide : uint8
{
	Front, Back
};

UCLASS(ClassGroup = "Gen", meta = (BlueprintSpawnableComponent, DisplayName = "Sector Socket"))
class TEAMPROJECT_FIN_API USectorSocketComponent : public UArrowComponent
{
	GENERATED_BODY()
public:
	USectorSocketComponent()
	{
		ArrowSize = 3.f;
		ArrowLength = 200.f;
		SetHiddenInGame(true);
	}

	UPROPERTY(EditAnywhere, Category = "Gen")
	ESocketSide Side = ESocketSide::Front;
	
	UPROPERTY(EditAnywhere, Category = "Gen")
	int32 SectorSizeX = 0;
	
	UPROPERTY(EditAnywhere, Category = "Gen")
	int32 SectorSizeY = 0;
};