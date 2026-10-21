#include "DoorEvent.h"

#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

ADoorEvent::ADoorEvent()
{
	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	RootComponent = FrameMesh;

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(FrameMesh);

	//문짝은 각 머신의 OnOpen 이 직접 움직인다. 컴포넌트 복제를 켜면 서버 위치와 로컬 연출이 서로 덮어써 떨린다
	bReplicates = true;
}

void ADoorEvent::Execute(const UObject* WorldContext)
{
	Super::Execute(WorldContext);	//PreEvents 발화

	bOpened = true;
	OnOpen();	//RepNotify 는 서버 자신에게는 오지 않는다
}

void ADoorEvent::FinishOpen()
{
	if (!HasAuthority())
	{
		return;
	}
	RunEventGroups(PostEvents);
}

void ADoorEvent::OnRep_Opened()
{
	OnOpen();
}

void ADoorEvent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADoorEvent, bOpened);
}
