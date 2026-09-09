#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Engine/DataAsset.h"
#include "SectorEvent.generated.h"

UINTERFACE(MinimalAPI)
class USectorEvent : public UInterface
{
	GENERATED_BODY()
};

class ISectorEvent
{
	GENERATED_BODY()

public:
	//ConceptEvent는 자체 월드가 없어서 호출자를 컨텍스트로 받는다
	virtual void Execute(const UObject* WorldContext) {}
};

//같은 타이밍에 나갈 이벤트 묶음
USTRUCT(BlueprintType)
struct FTimedEventGroup
{
	GENERATED_BODY()

	//기준 시점으로부터의 지연. 0 이하면 즉시 실행
	UPROPERTY(EditInstanceOnly, Category = "Sector")
	float Delay = 0.f;

	//ConceptEvent 데이터 에셋용
	UPROPERTY(EditInstanceOnly, Category = "Sector")
	TArray<TScriptInterface<ISectorEvent>> Events;	//본인을 지정 시 재귀가 발생할 수 있음

	//레벨 배치 액터용. 인터페이스 픽커는 액터를 못 잡으므로 클래스를 직접 제한한다. 인터페이스를 구현한 액터 베이스가 늘면 여기에 추가
	UPROPERTY(EditInstanceOnly, Category = "Sector",
		meta = (
			AllowedClasses = 
			"/Script/TeamProject_Fin.ScenePlacedEvent,/Script/TeamProject_Fin.ChaosCacheTriggerEvent"
		)
	)
	TArray<TScriptInterface<ISectorEvent>> PlacedEvents;	//본인을 지정 시 재귀가 발생할 수 있음

	//두 배열을 합쳐서 반환. 실행부는 항상 이걸 순회한다
	TArray<TScriptInterface<ISectorEvent>> AllEvents() const
	{
		TArray<TScriptInterface<ISectorEvent>> Result = Events;
		Result.Append(PlacedEvents);
		return Result;
	}
};

//씬에 배치되지 않지만 데이터는 필요한 이벤트. 사운드 재생, 컷신 재생 등의 이벤트를 구현할 때 사용
UCLASS(Abstract, BlueprintType)
class UConceptEvent : public UDataAsset, public ISectorEvent
{
	GENERATED_BODY()
};

//씬에 배치되어 사용되는 이벤트. 가까이 가면 전투 진행, 전투 승리 후 보상 등 씬에 존재하는 오브젝트와 상호작용해야 하는 상황에 사용
UCLASS()
class AScenePlacedEvent : public AActor, public ISectorEvent
{
	GENERATED_BODY()

public:
	//Execute 시점 기준
	UPROPERTY(EditInstanceOnly, Category = "Sector")
	TArray<FTimedEventGroup> PreEvents;

	//종료 조건 충족 시점 기준. 언제 터뜨릴지는 파생 클래스가 정한다
	UPROPERTY(EditInstanceOnly, Category = "Sector")
	TArray<FTimedEventGroup> PostEvents;

	virtual void Execute(const UObject* WorldContext = nullptr) override { RunEventGroups(PreEvents); }

	//런타임에 종료 신호를 받을 이벤트를 끼워 넣는다. 지연 없이 바로 받는 그룹으로 붙인다
	void AddPostEvent(TScriptInterface<ISectorEvent> Event)
	{
		PostEvents.AddDefaulted_GetRef().PlacedEvents.Add(Event);
	}

protected:
	void RunEventGroups(const TArray<FTimedEventGroup>& Groups);

private :
	void ExecuteGroup(TArray<TScriptInterface<ISectorEvent>> Events);
};
