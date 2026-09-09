#include "SectorEvent.h"

#include "TimerManager.h"

void AScenePlacedEvent::RunEventGroups(const TArray<FTimedEventGroup>& Groups)
{
	for (const FTimedEventGroup& Group : Groups)
	{
		if (Group.Delay <= 0.f)
		{
			ExecuteGroup(Group.AllEvents());
			continue;
		}

		FTimerHandle Handle;	//액터가 파괴되면 타이머 매니저가 알아서 정리하므로 보관하지 않는다
		GetWorldTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateUObject(this, &AScenePlacedEvent::ExecuteGroup, Group.AllEvents()),
			Group.Delay, false);
	}
}

void AScenePlacedEvent::ExecuteGroup(TArray<TScriptInterface<ISectorEvent>> Events)
{
	for (const TScriptInterface<ISectorEvent>& Event : Events)
	{
		ISectorEvent* Impl = Event.GetInterface();
		if (!Impl || Impl == static_cast<ISectorEvent*>(this)) { continue; }	//재귀일경우 가드
		Impl->Execute(this);
	}
}
