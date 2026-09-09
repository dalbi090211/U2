#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Net/Core/Connection/NetEnums.h"
#include "GameCore/UI/UIStruct.h"

#include "MatchAlertSubsystem.generated.h"

/*
 * 접속이 끊겨 타이틀로 되돌아왔을 때 사유를 다음 맵까지 들고 간다.
 * 트래블에서 월드가 통째로 파괴되므로 GameInstance 수명이 필요하다.
 */
UCLASS()
class UMatchAlertSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 다음 맵에서 띄울 알림을 예약한다
	UFUNCTION(BlueprintCallable, Category = "Match")
	void SetPendingAlert(ELayerName Alert);

	// 예약된 알림을 한 번만 꺼내간다. 없으면 false
	UFUNCTION(BlueprintCallable, Category = "Match")
	bool ConsumePendingAlert(ELayerName& OutAlert);

private:
	void HandleNetworkFailure(
		UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	bool bHasPendingAlert = false;
	ELayerName PendingAlert = ELayerName::Alert_SessionClose;

	FDelegateHandle NetworkFailureHandle;
};
