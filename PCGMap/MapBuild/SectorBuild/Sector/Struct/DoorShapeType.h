#pragma once

#include "CoreMinimal.h"

#include "DoorShapeType.generated.h"

UENUM(BlueprintType)
enum class EDoorShapeType : uint8
{
	None	UMETA(DisplayName = "없음"),
	Small1	UMETA(DisplayName = "소형1"),
	Small2	UMETA(DisplayName = "소형2"),
	Middle1	UMETA(DisplayName = "중형1"),
	Middle2	UMETA(DisplayName = "중형2"),
	Large1	UMETA(DisplayName = "대형1"),
	Large2	UMETA(DisplayName = "대형2")
};
