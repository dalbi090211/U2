#include "GCN_PortalFade.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

AGCN_PortalFade::AGCN_PortalFade()
{
	// 페이드가 1에 닿으면 스스로 끈다
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bAutoDestroyOnRemove = false;
}

void AGCN_PortalFade::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!FadeMID)
	{
		SetActorTickEnabled(false);
		return;
	}

	FadeElapsed += DeltaSeconds;

	const float Alpha = FMath::Clamp(FadeElapsed / ActiveFadeDuration, 0.0f, 1.0f);
	FadeMID->SetScalarParameterValue(FadeParameterName, Alpha);

	if (Alpha >= 1.0f)
	{
		// 1에 도달한 뒤에는 Cue 가 제거될 때까지 흰 화면을 유지한다
		SetActorTickEnabled(false);
	}
}

bool AGCN_PortalFade::OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	ApplyFade(Parameters);
	return true;
}

bool AGCN_PortalFade::WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	// 늦게 관련성이 생긴 클라이언트는 OnActive 를 놓치고 여기로 들어온다
	ApplyFade(Parameters);
	return true;
}

bool AGCN_PortalFade::OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	RestoreFade();
	GameplayCueFinishedCallback();
	return true;
}

bool AGCN_PortalFade::Recycle()
{
	// 인스턴스가 재사용되므로 캐시를 반드시 비운다. 안 그러면 남의 카메라에 블렌더블이 남는다
	RestoreFade();
	return Super::Recycle();
}

void AGCN_PortalFade::ApplyFade(const FGameplayCueParameters& Parameters)
{
	if (FadeMID || !FadeMaterial)
	{
		return;
	}

	UCameraComponent* Camera = FindLocalCamera();
	if (!Camera)
	{
		return;
	}

	// GA_Interact 가 대상의 FadeDuration 을 RawMagnitude 로 실어 보낸다
	ActiveFadeDuration = Parameters.RawMagnitude > 0.0f ? Parameters.RawMagnitude : DefaultFadeDuration;

	FadeMID = UMaterialInstanceDynamic::Create(FadeMaterial, this);
	FadeMID->SetScalarParameterValue(FadeParameterName, 0.0f);
	Camera->AddOrUpdateBlendable(FadeMID, 1.0f);
	FadeCamera = Camera;

	FadeElapsed = 0.0f;
	SetActorTickEnabled(true);
}

void AGCN_PortalFade::RestoreFade()
{
	SetActorTickEnabled(false);
	FadeElapsed = 0.0f;

	// 어떤 경로로 끝나든 반드시 걷어낸다. 안 그러면 화면이 하얀 채로 남는다
	if (UCameraComponent* Camera = FadeCamera.Get())
	{
		Camera->PostProcessSettings.RemoveBlendable(FadeMID);
	}
	FadeCamera.Reset();
	FadeMID = nullptr;
}

UCameraComponent* AGCN_PortalFade::FindLocalCamera() const
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	// Cue 가 붙은 액터(포탈을 탄 플레이어)가 아니라 이 화면을 보고 있는 카메라를 잡는다
	APlayerController* LocalPC = World->GetFirstPlayerController();
	AActor* ViewTarget = LocalPC ? LocalPC->GetViewTarget() : nullptr;
	return ViewTarget ? ViewTarget->FindComponentByClass<UCameraComponent>() : nullptr;
}
