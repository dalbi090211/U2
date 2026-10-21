#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameCore/Audio/MusicParameterEvent.h"
#include "MapMusicTable.generated.h"

// 맵이 열릴 때 각 머신이 스스로 트는 음악. GameState 를 거치지 않아 GameMode 없는 맵에서도 된다
UCLASS()
class TEAMPROJECT_FIN_API UMapMusicTable : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Audio|Music")
	TMap<TSoftObjectPtr<UWorld>, TObjectPtr<UMusicParameterEvent>> MapMusic;
};
