#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ProjectAudioSettings.generated.h"

class UFMODEvent;
class UMapMusicTable;
class USoundControlBus;
class USoundControlBusMix;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Project Audio"))
class TEAMPROJECT_FIN_API UProjectAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category = "Music")
	TSoftObjectPtr<UFMODEvent> MainMusicEvent;

	UPROPERTY(EditAnywhere, Config, Category = "Music")
	FName MusicStateParameterName = TEXT("MusicState");

	// 비워 두면 맵별 음악을 쓰지 않는다
	UPROPERTY(EditAnywhere, Config, Category = "Music")
	TSoftObjectPtr<UMapMusicTable> MapMusicTable;

	UPROPERTY(EditAnywhere, Config, Category = "Volume")
	TSoftObjectPtr<USoundControlBusMix> UserMix;

	UPROPERTY(EditAnywhere, Config, Category = "Volume")
	TSoftObjectPtr<USoundControlBus> MasterBus;

	UPROPERTY(EditAnywhere, Config, Category = "Volume")
	TSoftObjectPtr<USoundControlBus> MusicBus;

	UPROPERTY(EditAnywhere, Config, Category = "Volume")
	TSoftObjectPtr<USoundControlBus> SFXBus;

	UPROPERTY(EditAnywhere, Config, Category = "Volume")
	TSoftObjectPtr<USoundControlBus> UIBus;
};
