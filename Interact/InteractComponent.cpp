#include "InteractComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameCore/Interface/Interactable.h"
#include "PCGMap/MapBuild/SectorBuild/Event/EventManager.h"

UInteractComponent::UInteractComponent()
{
	// 컴포넌트 RPC를 위해 복제 대상으로 둔다. 복제 프로퍼티는 없다
	SetIsReplicatedByDefault(true);
}

void UInteractComponent::BindInput(UInputComponent* PlayerInputComponent, UInputAction* InteractAction)
{
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC || !InteractAction)
	{
		return;
	}

	EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &UInteractComponent::Invoke);
}

void UInteractComponent::Register(AActor* Target)
{
	if (Target && Target->Implements<UInteractable>())
	{
		Candidates.AddUnique(Target);
		RefreshPrompt();
	}
}

void UInteractComponent::Unregister(AActor* Target)
{
	Candidates.Remove(Target);
	RefreshPrompt();
}

void UInteractComponent::RefreshPrompt()
{
	//Register/Unregister는 서버와 클라 양쪽에서 돈다. 표시는 자기 화면인 경우에만
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}

	UGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}

	const IInteractable* Target = Cast<IInteractable>(GetNearest());
	GameInstance->GetSubsystem<UEventManager>()->SetInteractPrompt(
		Target ? Target->GetInteractText() : FText::GetEmpty());
}

AActor* UInteractComponent::GetNearest() const
{
	const FVector Origin = GetOwner()->GetActorLocation();

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (AActor* Candidate : Candidates)
	{
		if (!IsValid(Candidate))
		{
			continue; // 범위 안에서 파괴된 액터
		}

		const float DistSq = FVector::DistSquared(Origin, Candidate->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}

	return Best;
}

void UInteractComponent::Invoke()
{
	if (!GetOwner()->HasAuthority())
	{
		Server_Invoke();
		return;
	}

	// 여기부터는 서버
	IInteractable* Target = Cast<IInteractable>(GetNearest());
	if (!Target)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	Target->Execute(Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr);
}

void UInteractComponent::Server_Invoke_Implementation()
{
	Invoke();
}
