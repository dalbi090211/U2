#include "BaseWidgetLayer.h"

void UBaseWidgetLayer::NativeConstruct()
{
	Super::NativeConstruct();

	// 화면 전체를 덮으므로 자기 자신은 히트 테스트에서 제외한다
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ApplyInputMode();
}

UDefaultWidgetLayer* UBaseWidgetLayer::ShowWidget(ELayerName Name)
{
	TObjectPtr<UDefaultWidgetLayer>* Found = Widgets.Find(Name);
	UDefaultWidgetLayer* Widget = Found ? Found->Get() : nullptr;

	if (!Widget)
	{
		const TSubclassOf<UDefaultWidgetLayer>* Class = WidgetClasses.FindByPredicate(
			[Name](const TSubclassOf<UDefaultWidgetLayer>& Candidate)
			{
				return Candidate && GetDefault<UDefaultWidgetLayer>(Candidate)->LayerName == Name;
			});

		if (!Class)
		{
			UE_LOG(LogTemp, Error, TEXT("[UI] LayerName %d 위젯 클래스가 %s의 WidgetClasses에 없다."),
				static_cast<int32>(Name), *GetClass()->GetName());
			return nullptr;
		}

		Widget = CreateWidget<UDefaultWidgetLayer>(GetOwningPlayer(), *Class);
		if (!Widget)
		{
			return nullptr;
		}

		Widget->AddToViewport(Stack.Num() + 1);
		Widgets.Add(Name, Widget);
	}

	Widget->Show();

	// 이미 떠 있던 창은 스택 맨 위로 올린다
	Stack.Remove(Widget);
	Stack.Add(Widget);

	ApplyInputMode();

	// 재활용된 위젯은 이전 상태가 남아 있다. 드러나는 쪽이 자기를 되돌린다.
	Widget->OnRepair();
	return Widget;
}

void UBaseWidgetLayer::CloseTop()
{
	if (Stack.Num() > 0)
	{
		// 재활용을 위해 Widgets에 남겨둔다
		if (UDefaultWidgetLayer* Top = Stack.Pop())
		{
			Top->Hide();
		}
		ApplyInputMode();

		// 다시 드러난 쪽이 자기 상태를 되돌린다. 스택이 비면 베이스가 드러난다.
		UDefaultWidgetLayer* Below = Stack.Num() > 0 ? Stack.Last() : nullptr;
		Below ? Below->OnRepair() : OnRepair();
		return;
	}

	ShowWidget(BackWidgetName);
}

void UBaseWidgetLayer::ApplyInputMode()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	if (Stack.Num() == 0 && bGameInputWhenNoWindow)
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
		return;
	}

	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
	PC->bShowMouseCursor = true;
}