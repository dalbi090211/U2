#pragma once

#include "CoreMinimal.h"
#include "GameCore/UI/DefaultWidgetLayer.h"

#include "BaseWidgetLayer.generated.h"

// 씬마다 하나만 존재하는 최하단 레이어. 위에 쌓이는 레이어의 생성/재활용/ESC 처리를 전담한다.
UCLASS(Abstract)
class UBaseWidgetLayer : public ULayerWidgetBase
{
	GENERATED_BODY()

public:
	// 이 씬에서 띄울 수 있는 레이어 목록. 어떤 LayerName인지는 각 WBP가 들고 있다.
	UPROPERTY(EditDefaultsOnly, Category = "Layer")
	TArray<TSubclassOf<UDefaultWidgetLayer>> WidgetClasses;

	// 스택이 빈 상태에서 ESC를 누르면 뜨는 위젯. 로비면 종료 확인, 게임플레이면 옵션.
	UPROPERTY(EditDefaultsOnly, Category = "Layer")
	ELayerName BackWidgetName = ELayerName::Option;

	// 창이 하나도 없을 때 Game Only 로 돌린다. 게임플레이 씬용.
	UPROPERTY(EditDefaultsOnly, Category = "Layer")
	bool bGameInputWhenNoWindow = false;

	// 이미 만들어 둔 게 있으면 재활용한다.
	UFUNCTION(BlueprintCallable, Category = "Layer")
	UDefaultWidgetLayer* ShowWidget(ELayerName Name);

	// ESC 동작. 스택이 비어 있으면 BackWidget을 띄운다.
	UFUNCTION(BlueprintCallable, Category = "Layer")
	void CloseTop();

protected:
	virtual void NativeConstruct() override;

private:
	// 재활용 풀. Hide 된 위젯도 포함
	UPROPERTY()
	TMap<ELayerName, TObjectPtr<UDefaultWidgetLayer>> Widgets;

	UPROPERTY()
	TArray<TObjectPtr<UDefaultWidgetLayer>> Stack;

	void ApplyInputMode();
};
