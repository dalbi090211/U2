#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "../EventManager.h"	//EScreenIntensity
#include "ScreenTextEvent.generated.h"

//화면에 텍스트 한 줄을 띄우는 이벤트. 에셋 하나를 여러 섹터가 공유할 수 있다
UCLASS()
class UScreenTextEvent : public UConceptEvent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "ScreenText")
	FText Text;

	UPROPERTY(EditAnywhere, Category = "ScreenText")
	EScreenIntensity Intensity = EScreenIntensity::Title;

	//페이드 인 길이보다 길어야 한다
	UPROPERTY(EditAnywhere, Category = "ScreenText")
	float Duration = 3.f;

	virtual void Execute(const UObject* WorldContext) override;
};
