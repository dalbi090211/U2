#pragma once

#include "CoreMinimal.h"
#include "GameCore/HeroType/DLHeroTypes.h"

#include "DLPlayerLoadOut.generated.h"

//로비 -> 게임맵 이동 시 유지시킬 데이터 구조체
USTRUCT(BlueprintType)
struct FDLPlayerLoadout
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString PlayerName;

	UPROPERTY(BlueprintReadOnly)
	EDLHero HeroType = EDLHero::Bruiser;

	// UPROPERTY(BlueprintReadOnly)
	// int32 Level = 1;
	//
	// UPROPERTY(BlueprintReadOnly)
	// TArray<FGameplayTag> Perks;
};
