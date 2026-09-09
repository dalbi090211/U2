#include "EventManager.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

void UEventManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UEventManager::OnPreLoadMap);
}

void UEventManager::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	Super::Deinitialize();
}

void UEventManager::OnPreLoadMap(const FString&)
{
	//타이머는 월드와 함께 죽고, 트리거의 EndOverlap도 오지 않는다. 상태만 비우고 브로드캐스트는 하지 않는다
	HideTimers.Empty();
	ActiveTexts.Empty();
	InteractPrompt = FText::GetEmpty();
	TimeLimitEndTime = 0.0;
	TimeLimitTickTimer.Invalidate();
}

void UEventManager::SetTimeLimit(float Remaining)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(TimeLimitTickTimer);

	if (Remaining <= 0.f)
	{
		TimeLimitEndTime = 0.0;
		OnTimeLimitChanged.Broadcast(0.f);
		return;
	}

	//끝나는 시각을 잡아두면 늦게 붙은 위젯도 남은 시간을 정확히 받아간다
	TimeLimitEndTime = World->GetTimeSeconds() + Remaining;
	World->GetTimerManager().SetTimer(
		TimeLimitTickTimer, this, &UEventManager::TickTimeLimit, TimeLimitUpdateInterval, true);
	OnTimeLimitChanged.Broadcast(Remaining);
}

void UEventManager::TickTimeLimit()
{
	const float Remaining = GetTimeLimitRemaining();
	if (Remaining <= 0.f)
	{
		//서버가 종료를 알리기 전에 0에 닿아도 숫자가 음수로 흐르지 않게 한다
		SetTimeLimit(0.f);
		return;
	}

	OnTimeLimitChanged.Broadcast(Remaining);
}

float UEventManager::GetTimeLimitRemaining() const
{
	const UWorld* World = TimeLimitEndTime > 0.0 ? GetWorld() : nullptr;
	return World ? FMath::Max(0.f, static_cast<float>(TimeLimitEndTime - World->GetTimeSeconds())) : 0.f;
}

void UEventManager::ShowScreenText(const FText& Text, EScreenIntensity Intensity, float Duration)
{
	ActiveTexts.Add(Intensity, Text);
	OnScreenTextRequested.Broadcast(Text, Intensity);

	UWorld* World = GetWorld();
	if (!World) { return; }

	FTimerHandle& Handle = HideTimers.FindOrAdd(Intensity);
	World->GetTimerManager().ClearTimer(Handle);
	if (Duration > 0.f)
	{
		World->GetTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateUObject(this, &UEventManager::HideScreenText, Intensity),
			Duration, false);
	}
}

void UEventManager::HideScreenText(EScreenIntensity Intensity)
{
	if (UWorld* World = GetWorld())
	{
		if (FTimerHandle* Handle = HideTimers.Find(Intensity))
		{
			World->GetTimerManager().ClearTimer(*Handle);
		}
	}
	ActiveTexts.Remove(Intensity);
	OnScreenTextHideRequested.Broadcast(Intensity);
}

void UEventManager::SetInteractPrompt(const FText& Prompt)
{
	if (Prompt.EqualTo(InteractPrompt))
	{
		return;
	}

	InteractPrompt = Prompt;
	OnInteractPromptChanged.Broadcast(InteractPrompt);
}

void UEventManager::ReplayActiveTexts()
{
	for (const TPair<EScreenIntensity, FText>& Active : ActiveTexts)
	{
		OnScreenTextRequested.Broadcast(Active.Value, Active.Key);
	}

	OnInteractPromptChanged.Broadcast(InteractPrompt);

	if (TimeLimitEndTime > 0.0)
	{
		OnTimeLimitChanged.Broadcast(GetTimeLimitRemaining());
	}
}
