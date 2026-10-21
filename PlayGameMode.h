#pragma once

#include "CoreMinimal.h"
#include "GameCore/BaseGameMode.h"
#include "GameCore/HeroType/DLHeroTypes.h"

#include "PlayGameMode.generated.h"

// 게임 맵용 게임모드. 로비를 벗어난 뒤의 규칙을 담당한다.
UCLASS()
class APlayGameMode : public ABaseGameMode
{
	GENERATED_BODY()

public:
	// PlayerState의 영웅에 맞는 폰을 스폰한다. 표에 없으면 DefaultPawnClass
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

	// 맵에 들어올 때마다 루트를 확보하고 갈 수 있는 섹터를 미리 올린다
	virtual void BeginPlay() override;

protected:
	// 영웅별 캐릭터 BP
	UPROPERTY(EditDefaultsOnly, Category = "Hero")
	TMap<EDLHero, TSoftClassPtr<APawn>> HeroPawnClasses;
};
