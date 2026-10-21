#include "BaseWidgetLayer.h"

#include "GameCore/BasePlayerController.h"

void UBaseWidgetLayer::NativeConstruct()
{
	Super::NativeConstruct();

	// 화면 전체를 덮으므로 자기 자신은 히트 테스트에서 제외한다
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ApplyInputMode();
}

UDefaultWidgetLayer* UBaseWidgetLayer::ShowWidget(ELayerName Name)
{
	if (bPresentationHidden)
	{
		return nullptr;
	}
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

	// 루트맵은 스택 밖에서 뜬다. 스택이 비었다고 게임 입력으로 되돌리면 클릭이 죽는다.
	ABasePlayerController* BasePC = Cast<ABasePlayerController>(PC);
	const bool bOutsideUI = BasePC && BasePC->HasUIOutsideLayers();

	if (Stack.Num() == 0 && !bOutsideUI)
	{
		if (bGameInputWhenNoWindow)
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
		return;
	}

	// 창이 떠 있는 동안은 게임 입력을 끊는다. ESC는 맨 위 위젯이 직접 받는다.
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	if (Stack.Num() > 0)
	{
		Mode.SetWidgetToFocus(Stack.Last()->TakeWidget());
	}
	PC->SetInputMode(Mode);
	PC->bShowMouseCursor = true;

	// 창을 열기 전에 누르고 있던 키가 눌린 채로 남아 캐릭터가 계속 움직이는 걸 막는다
	PC->FlushPressedKeys();
}

void UBaseWidgetLayer::SetPresentationHidden(bool bHidden)
{
	if (bPresentationHidden == bHidden)
	{
		return;
	}
	bPresentationHidden = bHidden;
	if (bHidden)
	{
		SavedPresentationVisibility.Add(this, GetVisibility());
		SetVisibility(ESlateVisibility::Collapsed);
		for (const auto& Pair : Widgets)
		{
			if (UDefaultWidgetLayer* Widget = Pair.Value.Get())
			{
				SavedPresentationVisibility.Add(Widget, Widget->GetVisibility());
				Widget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
	else
	{
		for (const auto& Pair : SavedPresentationVisibility)
		{
			if (UUserWidget* Widget = Pair.Key.Get())
			{
				Widget->SetVisibility(Pair.Value);
			}
		}
		SavedPresentationVisibility.Reset();
	}
}
