#pragma once

#include "CoreMinimal.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/Sector.h"
#include "PCGMap/MapBuild/SectorBuild/Event/SectorEvent.h"

#include "StartSector.generated.h"

// 시작 칸 전용 조각. 조립된 맵에 들어설 때 BGM 변경, 맵 안내 같은 이벤트를 건다
UCLASS()
class TEAMPROJECT_FIN_API AStartSector : public ASector
{
	GENERATED_BODY()

public:
	// 서버에서 조립이 끝난 직후 한 번 실행된다
	UPROPERTY(EditDefaultsOnly, Category = "Sector")
	TArray<TObjectPtr<UConceptEvent>> StartEvents;

	void ExecuteStartEvents()
	{
		for (UConceptEvent* Event : StartEvents)
		{
			checkf(Event, TEXT("[Sector] 시작 이벤트에 빈 칸: %s"), *GetClass()->GetName());
			Event->Execute(this);
		}
	}
};
