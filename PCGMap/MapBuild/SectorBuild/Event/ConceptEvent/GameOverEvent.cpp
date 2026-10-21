#include "GameOverEvent.h"

#include "Kismet/GameplayStatics.h"
#include "GameCore/BaseGameMode.h"
#include "GameCore/BaseGameState.h"

void UGameOverEvent::Execute(const UObject* WorldContext)
{
	// GameMode가 잡히면 서버다. 배치 이벤트가 서버에서만 Execute를 부른다
	ABaseGameMode* GameMode = Cast<ABaseGameMode>(UGameplayStatics::GetGameMode(WorldContext));
	if (!GameMode)
	{
		return;
	}

	ABaseGameState* GameState = Cast<ABaseGameState>(UGameplayStatics::GetGameState(WorldContext));

	// 이미 클리어했으면 제한시간 초과 등으로 늦게 온 게임오버는 무시한다
	if (GameState && GameState->IsGameCleared())
	{
		return;
	}

	GameMode->KillAllSurvivors();

	// EventManager는 머신마다 따로 있다. GameState를 거쳐야 클라에도 창이 뜬다
	if (GameState)
	{
		GameState->MulticastGameOver();
	}
}
