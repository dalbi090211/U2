#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "WaitAllEvent.generated.h"

//여러 이벤트의 합류점. WaitEvents의 PostEvents에 자신을 끼워 넣어
//전투 종료 같은 "완료" 신호를 Execute로 받는다. 다 모이면 자기 PostEvents를 발화한다
UCLASS()
class AWaitAllEvent : public AScenePlacedEvent
{
	GENERATED_BODY()

public:
	//완료 신호를 기다릴 이벤트들. BeginPlay에서 각자의 PostEvents에 자신을 등록한다
	UPROPERTY(EditInstanceOnly, Category = "WaitAll",
		meta = (AllowedClasses = "/Script/TeamProject_Fin.ScenePlacedEvent"))
	TArray<TScriptInterface<ISectorEvent>> WaitEvents;

	//끄면 정방향. 신호가 전부 모이는 순간 PostEvents를 발화한다
	//켜면 역방향. TimeLimit이 지날 때까지 다 모이지 않았으면 발화한다
	UPROPERTY(EditInstanceOnly, Category = "WaitAll")
	bool bReverse = false;

	//역방향 제한시간(초)
	UPROPERTY(EditInstanceOnly, Category = "WaitAll", meta = (EditCondition = "bReverse"))
	float TimeLimit = 60.f;

	//끄면 다른 이벤트가 Execute를 부를 때부터 잰다.
	//그 첫 호출은 시작 신호로만 쓰이므로 시작을 알리는 이벤트는 WaitEvents에 넣지 않는다
	UPROPERTY(EditInstanceOnly, Category = "WaitAll", meta = (EditCondition = "bReverse"))
	bool bStartOnBeginPlay = true;

	//신호 하나를 받는다. 호출자가 누구인지는 WorldContext로 온다
	virtual void Execute(const UObject* WorldContext = nullptr) override;

protected:
	virtual void BeginPlay() override;

private:
	bool AreAllSigned() const;
	void StartTimeLimit();
	void OnTimeLimitExpired();

	//역방향 제한시간을 모든 머신의 HUD에 알린다. 0 이하면 지운다
	void BroadcastTimeLimit(float Remaining);

	//신호를 보낸 이벤트들. 같은 이벤트가 두 번 불러도 한 번으로 센다
	TArray<TWeakObjectPtr<const UObject>> Signaled;

	bool bStarted = false;
	bool bFinished = false;
	FTimerHandle TimeLimitTimer;
};
