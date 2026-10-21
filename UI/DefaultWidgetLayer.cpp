#include "DefaultWidgetLayer.h"

#include "GameCore/BasePlayerController.h"

UDefaultWidgetLayer* UDefaultWidgetLayer::ShowWidget(ELayerName Name)
{
	ABasePlayerController* PC = Cast<ABasePlayerController>(GetOwningPlayer());
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[UI] %s의 OwningPlayer가 ABasePlayerController가 아니다."), *GetName());
		return nullptr;
	}

	return PC->ShowWidget(Name);
}

void UDefaultWidgetLayer::CloseWidget()
{
	if (ABasePlayerController* PC = Cast<ABasePlayerController>(GetOwningPlayer()))
	{
		PC->CloseWidget();
	}
}

void UDefaultWidgetLayer::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 포커스를 받아야 ESC 키가 들어온다
	SetIsFocusable(true);
}

FReply UDefaultWidgetLayer::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseWidget();
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
