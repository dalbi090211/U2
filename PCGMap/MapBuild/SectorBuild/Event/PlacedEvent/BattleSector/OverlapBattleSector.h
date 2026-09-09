#pragma once

#include "CoreMinimal.h"
#include "ConstBattleSector.h"
#include "Components/BoxComponent.h"
#include "OverlapBattleSector.generated.h"

class APawn;

//플레이어가 볼륨에 들어오면 몬스터를 스폰하는 전투 섹터
UCLASS()
class AOverlapBattleSector : public AConstBattleSector
{
	GENERATED_BODY()
public :
	AOverlapBattleSector();
	void RequestServerActivation(APawn* RequestingPawn);

private :
	UFUNCTION()
	void OnEnter(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	//크기는 레벨에 배치한 뒤 디테일 패널에서 조정
	UPROPERTY(VisibleAnywhere, Category = "BattleEvent")
	TObjectPtr<UBoxComponent> OverlapBox;

	bool bActivated = false;	//재진입 시 다시 스폰되는 것을 막는다
};
