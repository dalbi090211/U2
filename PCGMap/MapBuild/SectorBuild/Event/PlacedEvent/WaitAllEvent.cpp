#include "WaitAllEvent.h"

#include "GameCore/BaseGameState.h"
#include "TimerManager.h"

void AWaitAllEvent::BeginPlay()
{
	Super::BeginPlay();

	//대상의 PostEvents에 자신을 끼워 넣는다. 레벨에서는 WaitEvents만 채우면 된다
	for (const TScriptInterface<ISectorEvent>& Event : WaitEvents)
	{
		if (AScenePlacedEvent* Placed = Cast<AScenePlacedEvent>(Event.GetObject()))
		{
			Placed->AddPostEvent(this);
		}
	}

	//판정은 서버에서 한 번만. 연출 전파는 PostEvents 쪽이 알아서 한다
	if (bReverse && bStartOnBeginPlay && HasAuthority() && !WaitEvents.IsEmpty())
	{
		StartTimeLimit();
	}
}

void AWaitAllEvent::StartTimeLimit()
{
	bStarted = true;
	GetWorldTimerManager().SetTimer(
		TimeLimitTimer, this, &AWaitAllEvent::OnTimeLimitExpired, TimeLimit, false);
	BroadcastTimeLimit(TimeLimit);
}

void AWaitAllEvent::BroadcastTimeLimit(float Remaining)
{
	if (ABaseGameState* GameState = GetWorld()->GetGameState<ABaseGameState>())
	{
		GameState->MulticastTimeLimit(Remaining);
	}
}

void AWaitAllEvent::Execute(const UObject* WorldContext)
{
	if (bFinished || !HasAuthority())
	{
		return;
	}

	//첫 호출은 시간 재기를 시작하는 신호로만 쓴다
	if (bReverse && !bStartOnBeginPlay && !bStarted)
	{
		StartTimeLimit();
		return;
	}

	Signaled.AddUnique(WorldContext);
	if (!AreAllSigned())
	{
		return;
	}

	bFinished = true;
	GetWorldTimerManager().ClearTimer(TimeLimitTimer);

	//역방향은 실패했을 때만 터진다. 다 모였으면 성공이므로 표시만 지운다
	if (bReverse)
	{
		BroadcastTimeLimit(0.f);
		return;
	}

	RunEventGroups(PostEvents);
}

void AWaitAllEvent::OnTimeLimitExpired()
{
	if (bFinished)
	{
		return;
	}

	bFinished = true;
	BroadcastTimeLimit(0.f);
	RunEventGroups(PostEvents);
}

bool AWaitAllEvent::AreAllSigned() const
{
	for (const TScriptInterface<ISectorEvent>& Event : WaitEvents)
	{
		const UObject* EventObject = Event.GetObject();
		if (IsValid(EventObject) && !Signaled.Contains(EventObject))
		{
			return false;
		}
	}
	return true;
}
