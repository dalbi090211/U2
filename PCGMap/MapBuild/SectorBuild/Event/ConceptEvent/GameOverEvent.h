#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "GameOverEvent.generated.h"

//게임오버를 전역으로 알리는 이벤트. UI 등은 EventManager의 OnGameOver에 바인드한다
UCLASS()
class UGameOverEvent : public UConceptEvent
{
	GENERATED_BODY()

public:
	virtual void Execute(const UObject* WorldContext) override;
};
