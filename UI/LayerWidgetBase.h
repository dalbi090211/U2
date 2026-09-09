#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameCore/UI/UIStruct.h"

#include "LayerWidgetBase.generated.h"

class UWidgetAnimation;

// 모든 레이어 위젯의 공통 부모. 보이기/숨기기만 책임진다.
UCLASS(Abstract)
class ULayerWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	// WBP에서 지정한다. BaseWidgetLayer가 이 값으로 위젯을 찾고 재활용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Layer")
	ELayerName LayerName = ELayerName::Main;

	// WBP에 같은 이름의 애니메이션이 있으면 자동 연결되고, 없으면 null 이다.
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> FadeIn;

	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> FadeOut;

	UFUNCTION(BlueprintCallable, Category = "Layer")
	void Show();

	UFUNCTION(BlueprintCallable, Category = "Layer")
	void Hide();

	// 창을 닫은 뒤 원래 띄워야 할 레이어와 위젯 상태를 되돌린다. 씬마다 다르므로 BP에서 구현한다.
	UFUNCTION(BlueprintImplementableEvent, Category = "Layer")
	void OnRepair();

protected:
	virtual void NativeOnInitialized() override;

private:
	UFUNCTION()
	void OnFadeOutFinished();

	// FadeOut 재생 중 표시. 도중에 Show 되면 접지 않는다.
	bool bClosing = false;
};
