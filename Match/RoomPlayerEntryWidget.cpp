#include "RoomPlayerEntryWidget.h"

#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "GameCore/Match/LobbyPlayerController.h"

void URoomPlayerEntryWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!HeroCombo)
	{
		return;
	}

	const UEnum* HeroEnum = StaticEnum<EDLHero>();
	for (int32 i = 0; i < HeroEnum->NumEnums() - 1; ++i)
	{
		const EDLHero Hero = static_cast<EDLHero>(HeroEnum->GetValueByIndex(i));
		if (Hero == EDLHero::None)
		{
			continue;
		}

		HeroOptions.Add(Hero);
		HeroCombo->AddOption(HeroEnum->GetDisplayNameTextByIndex(i).ToString());
	}

	// 로드아웃 기본값과 초기 선택을 맞춘다
	const int32 DefaultIndex = HeroOptions.IndexOfByKey(FDLPlayerLoadout().HeroType);
	if (DefaultIndex != INDEX_NONE)
	{
		HeroCombo->SetSelectedIndex(DefaultIndex);
	}

	HeroCombo->OnSelectionChanged.AddDynamic(this, &URoomPlayerEntryWidget::OnHeroSelected);
}

void URoomPlayerEntryWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	IUserObjectListEntry::NativeOnListItemObjectSet(ListItemObject);

	SetData(Cast<URoomPlayerEntryData>(ListItemObject));
}

void URoomPlayerEntryWidget::SetData(URoomPlayerEntryData* InData)
{
	Data = InData;
	if (!Data)
	{
		return;
	}

	const EDLHero Hero = Data->Loadout.HeroType;

	if (PlayerNameText)
	{
		PlayerNameText->SetText(FText::FromString(Data->Loadout.PlayerName));
	}

	if (HeroNameText)
	{
		HeroNameText->SetText(Hero == EDLHero::None
			? NoHeroText
			: StaticEnum<EDLHero>()->GetDisplayNameTextByValue(static_cast<int64>(Hero)));
	}

	if (HeroCombo)
	{
		HeroCombo->SetIsEnabled(Data->bIsLocal);

		const int32 Index = HeroOptions.IndexOfByKey(Hero);
		if (Index == INDEX_NONE)
		{
			HeroCombo->ClearSelection();
		}
		else
		{
			HeroCombo->SetSelectedIndex(Index);
		}
	}
}

void URoomPlayerEntryWidget::OnHeroSelected(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// 코드에서 설정한 선택은 무시
	if (SelectionType == ESelectInfo::Direct)
	{
		return;
	}

	if (!Data || !Data->bIsLocal || !HeroCombo)
	{
		return;
	}

	if (!HeroOptions.IsValidIndex(HeroCombo->GetSelectedIndex()))
	{
		return;
	}

	if (ALobbyPlayerController* PC = Cast<ALobbyPlayerController>(GetOwningPlayer()))
	{
		PC->SelectHero(HeroOptions[HeroCombo->GetSelectedIndex()]);
	}
}
