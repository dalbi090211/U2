#pragma once

#include "CoreMinimal.h"
#include "GameCore/UI/DefaultWidgetLayer.h"
#include "GameCore/HeroType/DLHeroTypes.h"

#include "LobbyWaitRoomWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

USTRUCT(BlueprintType)
struct FHeroIntro
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	FText Title;

	UPROPERTY(EditDefaultsOnly)
	FText MainWeapon;

	UPROPERTY(EditDefaultsOnly, meta = (MultiLine = true))
	FText ClassDesc;

	UPROPERTY(EditDefaultsOnly)
	FText SkillTitle;

	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UTexture2D> SkillIcon;

	UPROPERTY(EditDefaultsOnly, meta = (MultiLine = true))
	FText SkillDesc;

	UPROPERTY(EditDefaultsOnly)
	TObjectPtr<UTexture2D> Background;
};

// 대기실. 로컬 플레이어가 고른 영웅의 소개글을 보여준다.
UCLASS(Abstract)
class ULobbyWaitRoomWidget : public UDefaultWidgetLayer
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION()
	void RefreshHeroIntro();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MainWeaponText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ClassDescText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SkillTitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SkillImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SkillDescText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> BackgroundImage;

	UPROPERTY(EditDefaultsOnly, Category = "Match")
	TMap<EDLHero, FHeroIntro> HeroIntros;
};
