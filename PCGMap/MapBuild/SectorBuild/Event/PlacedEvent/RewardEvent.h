#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "RewardEvent.generated.h"

UCLASS()
class ARewardEvent : public AScenePlacedEvent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditInstanceOnly, Category = "BattleEvent")
	TArray<UObject*> PosObjects;	//to-do : reward object 만들고 수정
};
