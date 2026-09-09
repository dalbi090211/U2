#include "SessionEntryWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "GameCore/Match/LobbyPlayerController.h"
#include "GameCore/Match/SessionEntryData.h"

void USessionEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (JoinButton)
	{
		JoinButton->OnClicked.AddDynamic(this, &USessionEntryWidget::JoinThis);
	}
}

void USessionEntryWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	IUserObjectListEntry::NativeOnListItemObjectSet(ListItemObject);

	SetData(Cast<USessionEntryData>(ListItemObject));
}

void USessionEntryWidget::SetData(USessionEntryData* InData)
{
	Data = InData;
	if (!Data)
	{
		return;
	}

	if (RoomNameText)
	{
		RoomNameText->SetText(FText::FromString(Data->RoomName));
	}

	if (PlayerCountText)
	{
		PlayerCountText->SetText(FText::FromString(
			FString::Printf(TEXT("%d / %d"), Data->CurrentPlayers.Num(), Data->MaxPlayers)));
	}

	if (PingText)
	{
		PingText->SetText(FText::FromString(FString::Printf(TEXT("%d ms"), Data->PingMs)));
	}

	if (PingIcon)
	{
		const FLinearColor PingColor = Data->PingMs <= GoodPingMs ? GoodPingColor
			: Data->PingMs <= OkPingMs ? OkPingColor
			: BadPingColor;
		PingIcon->SetColorAndOpacity(PingColor);
	}

	if (JoinButton)
	{
		// 정원이 찬 방은 비활성
		JoinButton->SetIsEnabled(Data->CurrentPlayers.Num() < Data->MaxPlayers);
	}

	OnDataSet();
}

void USessionEntryWidget::JoinThis()
{
	if (ALobbyPlayerController* PC = Cast<ALobbyPlayerController>(GetOwningPlayer()))
	{
		PC->JoinGame(Data);
	}
}
