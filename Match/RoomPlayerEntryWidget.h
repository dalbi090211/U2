#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "GameCore/UI/DLPlayerLoadOut.h"

#include "RoomPlayerEntryWidget.generated.h"

class UComboBoxString;
class UTextBlock;

// ListView에 넣기 위한 FDLPlayerLoadout 래퍼.
UCLASS(BlueprintType)
class URoomPlayerEntryData : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "Match")
	FDLPlayerLoadout Loadout;

	// 로컬 플레이어의 줄인지
	UPROPERTY(BlueprintReadOnly, Category = "Match")
	bool bIsLocal = false;
};

// 대기실 인원 목록 한 줄. 위젯 바인딩은 전부 Optional.
UCLASS(Abstract)
class URoomPlayerEntryWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

public:
	void SetData(URoomPlayerEntryData* InData);

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	TObjectPtr<URoomPlayerEntryData> Data;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

	UFUNCTION()
	void OnHeroSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UTextBlock> PlayerNameText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UTextBlock> HeroNameText;

	// 항목은 EDLHero에서 채운다. bIsLocal이 아니면 비활성.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UComboBoxString> HeroCombo;

	// HeroType이 None일 때 표시할 문구
	UPROPERTY(EditDefaultsOnly, Category = "Match")
	FText NoHeroText = NSLOCTEXT("Match", "NoHero", "Selecting...");

private:
	// HeroCombo 항목 순서 ↔ 영웅 값
	TArray<EDLHero> HeroOptions;
};
