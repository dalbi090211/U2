#pragma once

#include "CoreMinimal.h"
#include "MapBuild/SectorBuild/Event/PlacedEvent/PlacedInteractActor.h"
#include "MapTransObject.generated.h"

class UStaticMeshComponent;

UCLASS()
class AMapTransObject : public APlacedInteractActor
{
	GENERATED_BODY()

public:
	AMapTransObject();

	// 서버 전용. 진입 연출이 끝난 뒤 GA_Interact 가 부른다
	virtual void CompleteInteract() override;

	// 서버 전용. 한 명이 포탈을 타면 파티 전원의 화면에 루트맵이 열린다
	void OpenRouteMap();

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
