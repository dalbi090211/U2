#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"

#include "SessionEntryWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class USessionEntryData;

// 세션 리스트 한 줄. 위젯 바인딩은 전부 Optional.
UCLASS(Abstract)
class USessionEntryWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

public:
	// ListView 없이 직접 붙일 때 쓰는 세터
	UFUNCTION(BlueprintCallable, Category = "Match")
	void SetData(USessionEntryData* InData);

	// JoinButton이 있으면 자동으로 연결된다
	UFUNCTION(BlueprintCallable, Category = "Match")
	void JoinThis();

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	TObjectPtr<USessionEntryData> Data;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

	// 데이터 적용 후 BP 확장 지점
	UFUNCTION(BlueprintImplementableEvent, Category = "Match")
	void OnDataSet();

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UTextBlock> RoomNameText;

	// "2 / 4"
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UTextBlock> PlayerCountText;

	// "35 ms"
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UTextBlock> PingText;

	// 핑에 따라 색만 바꾼다. 브러시는 WBP에서 지정
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UImage> PingIcon;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Match")
	TObjectPtr<UButton> JoinButton;

	// 이하면 GoodPingColor, OkPingMs 이하면 OkPingColor, 초과면 BadPingColor
	UPROPERTY(EditDefaultsOnly, Category = "Match|Ping")
	int32 GoodPingMs = 60;

	UPROPERTY(EditDefaultsOnly, Category = "Match|Ping")
	int32 OkPingMs = 150;

	UPROPERTY(EditDefaultsOnly, Category = "Match|Ping")
	FLinearColor GoodPingColor = FLinearColor(0.2f, 0.9f, 0.3f);

	UPROPERTY(EditDefaultsOnly, Category = "Match|Ping")
	FLinearColor OkPingColor = FLinearColor(0.95f, 0.8f, 0.2f);

	UPROPERTY(EditDefaultsOnly, Category = "Match|Ping")
	FLinearColor BadPingColor = FLinearColor(0.9f, 0.25f, 0.25f);
};
