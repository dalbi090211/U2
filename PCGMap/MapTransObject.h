#pragma once

#include "CoreMinimal.h"
#include "MapBuild/SectorBuild/Event/PlacedEvent/PlacedInteractActor.h"
#include "MapTransObject.generated.h"

class UAudioComponent;
class USoundBase;
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
	virtual void BeginPlay() override;
	virtual void OnRep_Active() override;
	virtual void OnRep_Triggered() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gen")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 채널이 하나뿐이라 새 소리가 이전 소리를 끊는다
	UPROPERTY(VisibleAnywhere, Category = "Gen|Sound")
	TObjectPtr<UAudioComponent> Audio;

	// 들리는 범위는 사운드의 감쇠 설정이 정한다. 루프 사운드를 넣는다
	UPROPERTY(EditAnywhere, Category = "Gen|Sound")
	TObjectPtr<USoundBase> AmbientSound;

	UPROPERTY(EditAnywhere, Category = "Gen|Sound")
	TObjectPtr<USoundBase> InteractSound;

	UPROPERTY(EditAnywhere, Category = "Gen|Sound")
	TObjectPtr<USoundBase> RouteMapOpenSound;

private:
	// 루트맵은 파티 전원에게 열리므로 소리도 전원에게 낸다
	UFUNCTION(NetMulticast, Reliable)
	void MulticastRouteMapOpened();

	void PlayExclusive(USoundBase* Sound);
};
