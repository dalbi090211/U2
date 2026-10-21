#include "ShowClearEvent.h"

#include "GameCore/BaseGameState.h"
#include "Kismet/GameplayStatics.h"

void UShowClearEvent::Execute(const UObject* WorldContext)
{
	if (ABaseGameState* GameState = Cast<ABaseGameState>(UGameplayStatics::GetGameState(WorldContext)))
	{
		GameState->MulticastShowClear(Text, Duration);
	}
}
