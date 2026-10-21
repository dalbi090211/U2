#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/DoorShapeType.h"

#include "Door.generated.h"

class UStaticMeshComponent;

// 조각과 조각이 만나는 자리에 서는 문. 조각에는 구멍만 뚫려 있다
UCLASS()
class TEAMPROJECT_FIN_API ADoor : public AActor
{
	GENERATED_BODY()

public:
	ADoor();

	UPROPERTY(EditAnywhere, Category = "Sector")
	EDoorShapeType DoorType = EDoorShapeType::None;

	// 열리면 사라지는 문짝. 문틀은 BP 가 따로 붙인다
	UPROPERTY(VisibleAnywhere, Category = "Sector")
	TObjectPtr<UStaticMeshComponent> Panel;

	// 이어진 문은 열어 둔다. 문짝을 숨기고 충돌을 꺼서 지나갈 수 있게 한다
	UFUNCTION(BlueprintCallable)
	void Open();
};
