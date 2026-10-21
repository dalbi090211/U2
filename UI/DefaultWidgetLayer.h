#pragma once

#include "CoreMinimal.h"
#include "GameCore/UI/LayerWidgetBase.h"

#include "DefaultWidgetLayer.generated.h"

// 베이???�에 ?�이???�반 ?�이?? ESC�?pop ?�다.
UCLASS(Abstract)
class UDefaultWidgetLayer : public ULayerWidgetBase
{
	GENERATED_BODY()

public:
	// ???�이???�의 버튼?�서 ?�른 ?�이?��? ?�울 ???�다
	UFUNCTION(BlueprintCallable, Category = "Layer")
	UDefaultWidgetLayer* ShowWidget(ELayerName Name);

	// ?�택 �????�나�?pop ?�고 Collapsed ?�킨??
	UFUNCTION(BlueprintCallable, Category = "Layer")
	void CloseWidget();

protected:
	virtual void NativeOnInitialized() override;

	// UIOnly 동안 PlayerController의 ESC 바인딩이 죽으므로 여기서 받는다
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
};
