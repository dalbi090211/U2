// BaseGameMode.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "BaseGameMode.generated.h"

UCLASS(abstract)
class ABaseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABaseGameMode();

	// 접속한 플레이어의 표시 이름을 로드아웃에 채운다
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	bool ReturnPartyToRoom();

protected:
	// 대기방을 거치지 않고 테스트 맵에서 시작했을 때의 복귀 목적지.
	UPROPERTY(EditDefaultsOnly, Category = "Match")
	FName DefaultReturnRoomMap = TEXT("LobbyTestMap");

public:

	// 게임오버 시 살아있는 플레이어를 즉사시킨다. 서버 전용
	void KillAllSurvivors();
};
