#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "GCN_PortalFade.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/*
 * 포탈 진입 화면 페이드.
 *
 * Cue 는 모든 머신에서 돌기 때문에 한 명이 포탈을 타면 파티 전원의 화면이 같이 하얘진다.
 * 대상은 Cue 가 붙은 액터가 아니라 그 머신의 로컬 뷰타겟 카메라다.
 */
UCLASS(Blueprintable)
class TEAMPROJECT_FIN_API AGCN_PortalFade : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()

public:
	AGCN_PortalFade();

	virtual void Tick(float DeltaSeconds) override;

	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool Recycle() override;

protected:
	// M_PostWhite. Material Domain 이 Post Process 여야 한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|Fade")
	TObjectPtr<UMaterialInterface> FadeMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|Fade")
	FName FadeParameterName = FName("Fade");

	// Cue 파라미터로 시간이 안 넘어왔을 때만 쓰는 값. 보통은 GA_Interact 가 정한다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|Fade", meta = (ClampMin = "0.01"))
	float DefaultFadeDuration = 1.0f;

private:
	void ApplyFade(const FGameplayCueParameters& Parameters);
	void RestoreFade();

	// 이 머신에서 실제로 화면을 그리는 카메라. 데디 서버면 없다
	UCameraComponent* FindLocalCamera() const;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FadeMID;

	TWeakObjectPtr<UCameraComponent> FadeCamera;
	float FadeElapsed = 0.0f;
	float ActiveFadeDuration = 1.0f;
};
