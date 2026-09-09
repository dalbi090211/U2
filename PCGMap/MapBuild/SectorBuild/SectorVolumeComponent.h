#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"

#include "SectorVolumeComponent.generated.h"

UCLASS(ClassGroup = "Gen", meta = (BlueprintSpawnableComponent, DisplayName = "Sector Volume"))
class TEAMPROJECT_FIN_API USectorVolumeComponent : public UBoxComponent
{
	GENERATED_BODY()
public:
	USectorVolumeComponent()
	{
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShapeColor = FColor::Cyan;
		bDrawOnlyIfSelected = false;
		SetHiddenInGame(true);
	}
};