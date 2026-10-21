#include "HeroPreviewActor.h"

#include "Components/SceneCaptureComponent2D.h"
#include "GameCore/BaseGameState.h"

AHeroPreviewActor::AHeroPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(GetRootComponent());
	// 로비 배경은 빼고 이 액터만 찍는다. 알파에 역불투명도가 들어가 UI 머티리얼에서 배경을 투명하게 뺄 수 있다
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	// 아이들 애니메이션이 움직이므로 매 프레임 찍는다
	Capture->bCaptureEveryFrame = true;
	// 자동 노출이 켜져 있으면 빈 배경 때문에 밝기가 계속 흔들린다
	Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Capture->PostProcessSettings.AutoExposureMinBrightness = 1.f;
	Capture->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
}

void AHeroPreviewActor::BeginPlay()
{
	Super::BeginPlay();

	Capture->ShowOnlyActorComponents(this);

	// 클라의 레벨 BeginPlay는 GameState 복제가 도착해야 시작되므로 여기서는 항상 있다
	ABaseGameState* GS = GetWorld()->GetGameState<ABaseGameState>();
	checkf(GS, TEXT("[HeroPreview] BaseGameState 없음"));

	// 인원 변경과 로드아웃 변경이 모두 이 신호로 온다
	GS->OnRoomPlayersChanged.AddDynamic(this, &AHeroPreviewActor::RefreshHero);
	RefreshHero();
}

void AHeroPreviewActor::RefreshHero()
{
	const EDLHero Hero = GetWorld()->GetGameState<ABaseGameState>()->GetLocalHero();
	checkf(Hero == EDLHero::None || HeroMeshes.Contains(Hero), TEXT("[HeroPreview] HeroMeshes에 영웅 없음: Hero=%d"), (int32)Hero);

	for (const TPair<EDLHero, FComponentReference>& Pair : HeroMeshes)
	{
		USceneComponent* HeroMesh = Cast<USceneComponent>(Pair.Value.GetComponent(this));
		checkf(HeroMesh, TEXT("[HeroPreview] HeroMeshes 컴포넌트 못 찾음: Hero=%d"), (int32)Pair.Key);

		HeroMesh->SetVisibility(Pair.Key == Hero, true);
	}
}
