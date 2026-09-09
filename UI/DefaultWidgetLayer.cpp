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
