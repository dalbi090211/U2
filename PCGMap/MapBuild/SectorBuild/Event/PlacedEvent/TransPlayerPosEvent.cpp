#include "TransPlayerPosEvent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

void ATransPlayerPosEvent::Execute(const UObject* WorldContext)
{
	Super::Execute(WorldContext);

	// 옮기는 것은 서버가 정한다. 클라가 제 폰을 옮기면 서버 위치로 되돌아간다
	if (!HasAuthority())
	{
		return;
	}

	const FTransform Target = GetActorTransform();
	int32 Index = 0;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}

		Pawn->TeleportTo(
			Target.GetLocation() + Target.GetRotation().GetRightVector() * (Index++ * SpreadSpacing),
			Target.Rotator());
	}

	RunEventGroups(PostEvents);
}
