#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "ShowClearEvent.generated.h"

//클리어 문구를 띄우는 이벤트. UI는 EventManager 의 OnShowClear 에 바인딩한다
UCLASS()
class UShowClearEvent : public UConceptEvent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "ShowClear")
	FText Text;

	// 클리어 배너의 표시 시간입니다. 초 단위로 설정합니다.
	UPROPERTY(EditAnywhere, Category = "ShowClear", meta = (ClampMin = "0.1", Units = "s"))
	float Duration = 3.0f;

	virtual void Execute(const UObject* WorldContext) override;
};
