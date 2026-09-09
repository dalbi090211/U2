#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "LevelTriggerEvent.generated.h"

//레벨 진입 시 PreEvents를 발화시킨다. 맵 이름/설명 표시 같은 진입 연출용
UCLASS()
class ALevelTriggerEvent : public AScenePlacedEvent
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};
