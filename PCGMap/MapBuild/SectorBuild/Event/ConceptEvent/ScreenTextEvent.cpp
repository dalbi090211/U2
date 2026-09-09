#include "ScreenTextEvent.h"

#include "GameCore/BaseGameState.h"
#include "Kismet/GameplayStatics.h"

void UScreenTextEvent::Execute(const UObject* WorldContext)
{
	if (ABaseGameState* GameState = Cast<ABaseGameState>(UGameplayStatics::GetGameState(WorldContext)))
	{
		GameState->MulticastScreenText(Text, Intensity, Duration);
	}
}
