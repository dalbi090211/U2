#include "MatchAlertSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

void UMatchAlertSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine)
	{
		NetworkFailureHandle =
			GEngine->OnNetworkFailure().AddUObject(this, &UMatchAlertSubsystem::HandleNetworkFailure);
	}
}

void UMatchAlertSubsystem::Deinitialize()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
	}

	Super::Deinitialize();
}

void UMatchAlertSubsystem::SetPendingAlert(ELayerName Alert)
{
	bHasPendingAlert = true;
	PendingAlert = Alert;
}

bool UMatchAlertSubsystem::ConsumePendingAlert(ELayerName& OutAlert)
{
	if (!bHasPendingAlert)
	{
		return false;
	}

	OutAlert = PendingAlert;
	bHasPendingAlert = false;
	return true;
}

void UMatchAlertSubsystem::HandleNetworkFailure(
	UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	// 방장 쪽 실패는 알릴 대상이 자기 자신뿐이라 다루지 않는다
	if (!World || World->GetNetMode() != NM_Client)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Match] 접속 끊김. Failure=%d %s"),
		static_cast<int32>(FailureType), *ErrorString);

	// 엔진이 알아서 기본 맵으로 되돌린다. 도착한 뒤 띄우도록 예약만 해둔다.
	SetPendingAlert(ELayerName::Alert_SessionClose);
}
