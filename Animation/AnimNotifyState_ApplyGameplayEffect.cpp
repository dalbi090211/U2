// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNotifyState_ApplyGameplayEffect.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Actor.h"

UAnimNotifyState_ApplyGameplayEffect::UAnimNotifyState_ApplyGameplayEffect()
{
	// 에디터에서 식별하기 쉬운 색상으로 지정 (선택사항)
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 128, 0, 255); // GE는 주황색 계열로 표시
#endif
}

void UAnimNotifyState_ApplyGameplayEffect::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	if (MeshComp && MeshComp->GetOwner() && GameplayEffectClass)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(MeshComp->GetOwner()))
		{
			FGameplayEffectContextHandle ContextHandle = ASC->MakeEffectContext();
			ContextHandle.AddInstigator(MeshComp->GetOwner(), MeshComp->GetOwner());

			FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(GameplayEffectClass, 1.0f, ContextHandle);
			if (SpecHandle.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}
}

void UAnimNotifyState_ApplyGameplayEffect::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (MeshComp && MeshComp->GetOwner() && GameplayEffectClass)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(MeshComp->GetOwner()))
		{
			// 멀티플레이어 환경(여러 명이 동시에 구르기)을 지원하기 위해, 
			// 멤버 변수에 핸들을 저장하지 않고 클래스 기반으로 해당 액터에 적용된 동일 GE를 전부 지웁니다.
			ASC->RemoveActiveGameplayEffectBySourceEffect(GameplayEffectClass, ASC);
		}
	}
}
