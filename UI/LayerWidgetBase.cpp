#include "LayerWidgetBase.h"

#include "Animation/WidgetAnimation.h"

void ULayerWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (FadeOut)
	{
		FWidgetAnimationDynamicEvent Finished;
		Finished.BindDynamic(this, &ULayerWidgetBase::OnFadeOutFinished);
		BindToAnimationFinished(FadeOut, Finished);
	}
}

void ULayerWidgetBase::Show()
{
	bClosing = false;

	if (FadeOut)
	{
		StopAnimation(FadeOut);
	}

	SetVisibility(ESlateVisibility::Visible);

	if (FadeIn)
	{
		PlayAnimation(FadeIn);
	}
}

void ULayerWidgetBase::Hide()
{
	// 애니메이션이 없거나 이미 접혀 있으면 바로 접는다
	if (!FadeOut || GetVisibility() == ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	if (bClosing)
	{
		return;
	}

	bClosing = true;
	PlayAnimation(FadeOut);
}

void ULayerWidgetBase::OnFadeOutFinished()
{
	if (bClosing)
	{
		bClosing = false;
		SetVisibility(ESlateVisibility::Collapsed);
	}
}
