#include "PlacedInteractActor.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameCore/Interact/InteractTriggerComponent.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

APlacedInteractActor::APlacedInteractActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	//메시는 BP에서 Add Component로 붙인다
	Trigger = CreateDefaultSubobject<UInteractTriggerComponent>(TEXT("Trigger"));
	RootComponent = Trigger;

	InteractEventTag = GameplayTagSystem::Event_Interact_Portal;
}

void APlacedInteractActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlacedInteractActor, bTriggered);
	DOREPLIFETIME(APlacedInteractActor, bActive);
}

void APlacedInteractActor::BeginPlay()
{
	Super::BeginPlay();

	//bStartActive는 레벨 데이터라 양쪽 머신이 같은 값을 갖는다. 열리는 순간만 복제된다
	bActive = bActive || bStartActive;
	if (!bActive)
	{
		SetActorHiddenInGame(true);
		Trigger->DisableInteraction();
	}
}

void APlacedInteractActor::ActivateInteract()
{
	if (bActive || !HasAuthority())
	{
		return;
	}

	bActive = true;
	OnRep_Active();	//서버는 복제 콜백이 오지 않는다
}

void APlacedInteractActor::OnRep_Active()
{
	SetActorHiddenInGame(false);

	//이미 소진된 뒤에 늦게 들어온 클라이언트가 다시 쓰지 못하게 한다
	if (!bOnce || !bTriggered)
	{
		Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	OnInteractActivated();
}

void APlacedInteractActor::Execute(APlayerController* PC)
{
	if (!bActive || (bOnce && bTriggered))
	{
		return;
	}
	bTriggered = true;
	OnRep_Triggered();	//서버는 복제 콜백이 오지 않는다

	UAbilitySystemComponent* ASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PC ? PC->GetPawn() : nullptr);

	FGameplayEventData Data;
	Data.OptionalObject = this;
	const int32 Triggered = (ASC && InteractEventTag.IsValid())
		? ASC->HandleGameplayEvent(InteractEventTag, &Data)
		: 0;

	//연출용 어빌리티가 없으면 그냥 발화한다. 안 그러면 진행이 통째로 막힌다
	if (Triggered == 0)
	{
		CompleteInteract();
	}
}

void APlacedInteractActor::CompleteInteract()
{
	RunEventGroups(PreEvents);
}

void APlacedInteractActor::OnRep_Triggered()
{
	if (bOnce && bTriggered)
	{
		Trigger->DisableInteraction();
	}
}
