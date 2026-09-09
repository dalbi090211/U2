#pragma once

#include "CoreMinimal.h"
#include "Chaos/CacheManagerActor.h"
#include "../SectorEvent.h"
#include "ChaosCacheTriggerEvent.generated.h"

//Execute 시 기록된 파괴 시뮬레이션을 재생하는 연출용 이벤트. 트리거 이벤트의 Events 배열에 몬스터 스폰과 함께 넣어 쓴다
//AChaosCacheManager도 AActor라서 AScenePlacedEvent와 동시에 상속할 수 없다. 인터페이스만 직접 구현한다
UCLASS()
class AChaosCacheTriggerEvent : public AChaosCacheManager, public ISectorEvent
{
	GENERATED_BODY()

public:
	AChaosCacheTriggerEvent();

	virtual void Execute(const UObject* WorldContext = nullptr) override { TriggerAll(); }
};
