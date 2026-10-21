#include "LobbyWaitRoomWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "GameCore/BaseGameState.h"
#include "GameCore/Match/LobbyPlayerController.h"

void ULobbyWaitRoomWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ALobbyPlayerController* PC = Cast<ALobbyPlayerController>(GetOwningPlayer());
	checkf(PC, TEXT("[WaitRoom] %s의 OwningPlayer가 ALobbyPlayerController가 아니다"), *GetName());

	PC->OnRoomUpdated.AddDynamic(this, &ULobbyWaitRoomWidget::RefreshHeroIntro);
	RefreshHeroIntro();
}

void ULobbyWaitRoomWidget::RefreshHeroIntro()
{
	// 클라는 접속 직후 GameState가 없다. 도착하면 OnRoomUpdated가 다시 부른다
	const ABaseGameState* GS = GetWorld()->GetGameState<ABaseGameState>();
	const EDLHero Hero = GS ? GS->GetLocalHero() : EDLHero::None;

	if (Hero == EDLHero::None)
	{
		TitleText->SetText(FText::GetEmpty());
		MainWeaponText->SetText(FText::GetEmpty());
		ClassDescText->SetText(FText::GetEmpty());
		SkillTitleText->SetText(FText::GetEmpty());
		SkillDescText->SetText(FText::GetEmpty());
		// 텍스처 없는 브러시는 흰 사각형으로 그려진다
		BackgroundImage->SetVisibility(ESlateVisibility::Hidden);
		SkillImage->SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	const FHeroIntro* Intro = HeroIntros.Find(Hero);
	checkf(Intro, TEXT("[WaitRoom] HeroIntros에 영웅 없음: Hero=%d"), (int32)Hero);

	TitleText->SetText(Intro->Title);
	MainWeaponText->SetText(Intro->MainWeapon);
	ClassDescText->SetText(Intro->ClassDesc);
	SkillTitleText->SetText(Intro->SkillTitle);
	SkillDescText->SetText(Intro->SkillDesc);
	BackgroundImage->SetBrushFromTexture(Intro->Background);
	BackgroundImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	SkillImage->SetBrushFromTexture(Intro->SkillIcon);
	SkillImage->SetVisibility(ESlateVisibility::HitTestInvisible);
}
