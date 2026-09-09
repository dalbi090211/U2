#include "DoorEvent.h"

#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

ADoorEvent::ADoorEvent()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	RootComponent = FrameMesh;

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(FrameMesh);

	//서버에서 Execute돼도 클라이언트가 문이 열리는 것을 보도록 한다
	bReplicates = true;
	DoorMesh->SetIsReplicated(true);
}

void ADoorEvent::Execute(const UObject* WorldContext)
{
	Super::Execute(WorldContext);	//PreEvents 발화

	if (OpenSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, OpenSound, GetActorLocation());
	}
	if (OpenEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this, OpenEffect, GetActorLocation(), GetActorRotation());
	}

	SetActorTickEnabled(true);
}

void ADoorEvent::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Step = FMath::Min(RaiseSpeed * DeltaSeconds, RaiseHeight - RaisedAmount);
	DoorMesh->AddLocalOffset(FVector(0.f, 0.f, Step));
	RaisedAmount += Step;

	if (RaisedAmount >= RaiseHeight)
	{
		SetActorTickEnabled(false);
		RunEventGroups(PostEvents);
	}
}
