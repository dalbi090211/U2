#pragma once

#include "CoreMinimal.h"

#include "DLHeroTypes.generated.h"

UENUM(BlueprintType)
enum class EDLHero : uint8
{
	None    = 0,
	Bruiser  = 1,
	Bomber  = 2,
	Sniper  = 3,
};