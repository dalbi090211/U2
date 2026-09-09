#include "OverlapBattleSector.h"

#include "GameFramework/Pawn.h"
#include "GameCore/Character/PlayerCharacter.h"

AOverlapBattleSector::AOverlapBattleSector()
{
	PrimaryActorTick.bCanEverTick = false;

	OverlapBox = CreateDefaultSubobject<UBoxComponent>(TEXT("OverlapBox"));
	OverlapBox->InitBoxExtent(FVector(500.f, 500.f, 300.f));
	OverlapBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OverlapBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	OverlapBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	OverlapBox->ShapeColor = FColor::Red;
	RootComponent = OverlapBox;

	OverlapBox->OnComponentBeginOverlap.AddDynamic(this, &AOverlapBattleSector::OnEnter);
}

void AOverlapBattleSector::OnEnter(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	const APawn* Pawn = Cast<APawn>(Other);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}

	if (HasAuthority())
	{
		RequestServerActivation(const_cast<APawn*>(Pawn));
	}
	else if (Pawn->IsLocallyControlled())
	{
		if (APlayerCharacter* PlayerCharacter = Cast<APlayerCharacter>(const_cast<APawn*>(Pawn)))
		{
			PlayerCharacter->ServerRequestBattleSectorActivation(this);
		}
	}
}

void AOverlapBattleSector::RequestServerActivation(APawn* RequestingPawn)
{
	if (!HasAuthority() || bActivated || !IsValid(RequestingPawn)
		|| !RequestingPawn->IsPlayerControlled()
		|| !OverlapBox->IsOverlappingActor(RequestingPawn))
	{
		return;
	}

	bActivated = true;
	Execute(RequestingPawn);
}
