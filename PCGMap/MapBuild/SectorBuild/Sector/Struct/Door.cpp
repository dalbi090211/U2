#include "Door.h"

#include "Components/StaticMeshComponent.h"

ADoor::ADoor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(RootComponent);
}

void ADoor::Open()
{
	// 문짝 메시를 BP 가 안 꽂았으면 여기서 죽어야 한다. 그냥 두면 막힌 문을 열린 줄 알고 지나간다
	checkf(Panel->GetStaticMesh(), TEXT("[Sector] 문짝 메시 없음: %s"), *GetName());

	Panel->SetVisibility(false, true);
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
