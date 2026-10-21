#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameCore/HeroType/DLHeroTypes.h"

#include "HeroPreviewActor.generated.h"

class USceneCaptureComponent2D;

// 대기실 로컬 플레이어의 영웅을 렌더 타깃에 찍는다. UMG Image가 그 렌더 타깃을 보여준다.
UCLASS()
class AHeroPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AHeroPreviewActor();

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void RefreshHero();

	UPROPERTY(VisibleAnywhere, Category = "Preview")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	// 영웅마다 BP에 둔 메시 컴포넌트. 현재 영웅 것만 보인다
	UPROPERTY(EditDefaultsOnly, Category = "Preview", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.SkeletalMeshComponent"))
	TMap<EDLHero, FComponentReference> HeroMeshes;
};