#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Sector.generated.h"

class ADoor;

// 이 조각에 붙은 문 하나. TPair 는 UPROPERTY 로 못 써서 구조체로 뺐다
USTRUCT()
struct FSectorDoorSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Sector")
	TObjectPtr<ADoor> Door = nullptr;

	// 문 메시 바운드. 문 액터의 위치·회전 기준이고 크기는 월드 단위. Init 이 잰다
	UPROPERTY(VisibleAnywhere, Category = "Sector")
	FBox MeshBounds = FBox(ForceInit);
};

UCLASS()
class TEAMPROJECT_FIN_API ASector : public AActor
{
	GENERATED_BODY()
public:
	ASector();

	// 붙어 있는 문과 메시를 훑어 캐시한다. 부착이 끝난 뒤에 부른다
	void Init();

protected:
	virtual void BeginPlay() override;

public:

	// 이음매에 선 문. 액터라 컴포넌트가 아니라 붙은 액터에서 모은다
	UPROPERTY(VisibleAnywhere, Category = "Sector")
	TArray<FSectorDoorSlot> Doors;

	// 겹침 판정에 쓰는 박스. 액터의 위치·회전 기준이고 크기는 월드 단위. Init이 메시 바운드에서 뽑는다
	UPROPERTY(VisibleAnywhere, Category = "Sector")
	TArray<FBox> Volumes;

	// 볼륨 개수 상한. 2의 거듭제곱으로 올림해서 쓴다. 올리면 촘촘해지고 판정 비용이 는다
	UPROPERTY(EditAnywhere, Category = "Sector", meta = (ClampMin = "1"))
	int32 MaxVolumes = 8;
};
