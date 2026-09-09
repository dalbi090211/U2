#pragma once

#include "CoreMinimal.h"

//외부 함수에서 레이어 접근하기 위해 생성, 실제 매핑은 코드에서 블랙박스로 처리
UENUM(BlueprintType)
enum class ELayerName : uint8
{
	//Common
	Option,
	QuitConfirm,
	Loading,
	
	//Lobby
	LobbyMain,
	FindGame,
	MatchRoom,
	MakeRoom,
	
	//GamePlay
	Main,
	DamageOverlay,
	ReviveGauge,
	GameOver,

	//Error
	Alert_SessionFull,
	Alert_SessionLost,
	Alert_SessionClose,
};
