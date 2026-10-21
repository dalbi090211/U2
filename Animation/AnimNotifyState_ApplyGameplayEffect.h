// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayEffect.h"
#include "AnimNotifyState_ApplyGameplayEffect.generated.h"

/**
 * 몽타주 재생 구간(Begin ~ End) 동안 Gameplay Effect를 부여하여 완벽한 GAS 롤백과 예측을 지원하는 NotifyState
 */
UCLASS(meta = (DisplayName = "Apply Gameplay Effect (GAS)"))
class TEAMPROJECT_FIN_API UAnimNotifyState_ApplyGameplayEffect : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UAnimNotifyState_ApplyGameplayEffect();

	// 부여할 게임플레이 이펙트 클래스 (무적 버프 등)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameplayEffect")
	TSubclassOf<UGameplayEffect> GameplayEffectClass;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

private:
	// 부여된 이펙트 핸들 (End에서 지우기 위해 임시 저장)
	// 주의: AnimNotifyState는 하나만 인스턴스화되어 여러 액터에 공유되므로 여기에 상태를 저장하면 안 되지만,
	// 싱글플레이나 자기 자신에게만 임시로 작동하는 로직에는 편의상 사용되기도 합니다.
	// 하지만 멀티플레이어 환경의 안전성을 위해 MeshComp->GetOwner()의 ASC에서 직접 해제하는 방식을 사용합니다.
};
