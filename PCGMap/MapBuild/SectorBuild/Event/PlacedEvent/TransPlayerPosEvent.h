#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "TransPlayerPosEvent.generated.h"

// 플레이어 전원을 이 액터가 선 자리로 옮긴다
UCLASS()
class ATransPlayerPosEvent : public AScenePlacedEvent
{
	GENERATED_BODY()

public:
	virtual void Execute(const UObject* WorldContext = nullptr) override;

	// 같은 지점에 겹쳐 놓으면 캡슐끼리 밀어낸다. 오른쪽으로 이만큼씩 벌려 세운다
	UPROPERTY(EditInstanceOnly, Category = "Transport", meta = (ClampMin = "0.0"))
	float SpreadSpacing = 150.f;
};
