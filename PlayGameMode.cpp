#include "PlayGameMode.h"

#include "GameCore/BasePlayerState.h"
#include "GameCore/BaseGameState.h"
#include "PCGMap/MapBuild/WorldGenSubSystem.h"

UClass* APlayGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const ABasePlayerState* PS = InController ? InController->GetPlayerState<ABasePlayerState>() : nullptr;
	const TSoftClassPtr<APawn>* Found = PS ? HeroPawnClasses.Find(PS->GetHeroType()) : nullptr;

	if (Found)
	{
		if (UClass* PawnClass = Found->LoadSynchronous())
		{
			return PawnClass;
		}

		UE_LOG(LogTemp, Error, TEXT("[Hero] 폰 클래스 로드 실패: %s"), *Found->ToString());
	}

	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

void APlayGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (ABaseGameState* GS = GetGameState<ABaseGameState>())
	{
		GS->StartRunStatistics();
	}

	if (UWorldGenSubSystem* Sys = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>())
	{
		Sys->GetRunSeed(); // 아직 없으면 여기서 루트가 만들어진다
		Sys->PreloadReachableSectors();
	}
}
