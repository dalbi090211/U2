#pragma once

#include "CoreMinimal.h"

#include "SectorPieceType.generated.h"

UENUM(BlueprintType)
enum class ESectorPieceType : uint8
{
	Start		UMETA(DisplayName = "시작"),
	Joint		UMETA(DisplayName = "접합부"),
	Battle		UMETA(DisplayName = "전투"),
	End			UMETA(DisplayName = "끝"),
	Objective	UMETA(DisplayName = "범용")
};
