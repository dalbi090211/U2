#include "InteractTriggerComponent.h"

#include "InteractComponent.h"

#include "GameFramework/Pawn.h"

UInteractTriggerComponent::UInteractTriggerComponent()
{
	//반경은 배치 후 디테일 패널에서 조정
	InitSphereRadius(250.f);
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ShapeColor = FColor::Magenta;

	OnComponentBeginOverlap.AddDynamic(this, &UInteractTriggerComponent::OnEnter);
	OnComponentEndOverlap.AddDynamic(this, &UInteractTriggerComponent::OnLeave);
}

void UInteractTriggerComponent::RefreshPrompt()
{
	TArray<AActor*> Overlaps;
	GetOverlappingActors(Overlaps, APawn::StaticClass());

	for (AActor* Overlap : Overlaps)
	{
		if (UInteractComponent* Interact = Overlap->FindComponentByClass<UInteractComponent>())
		{
			Interact->RefreshPrompt();
		}
	}
}

void UInteractTriggerComponent::DisableInteraction()
{
	//콜리전만 꺼도 EndOverlap이 오지만, 순서를 보장하려고 직접 뺀다
	TArray<AActor*> Overlaps;
	GetOverlappingActors(Overlaps, APawn::StaticClass());

	for (AActor* Overlap : Overlaps)
	{
		if (UInteractComponent* Interact = Overlap->FindComponentByClass<UInteractComponent>())
		{
			Interact->Unregister(GetOwner());
		}
	}

	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UInteractTriggerComponent::OnEnter(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (UInteractComponent* Interact = Other ? Other->FindComponentByClass<UInteractComponent>() : nullptr)
	{
		Interact->Register(GetOwner());
	}
}

void UInteractTriggerComponent::OnLeave(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
	if (UInteractComponent* Interact = Other ? Other->FindComponentByClass<UInteractComponent>() : nullptr)
	{
		Interact->Unregister(GetOwner());
	}
}
