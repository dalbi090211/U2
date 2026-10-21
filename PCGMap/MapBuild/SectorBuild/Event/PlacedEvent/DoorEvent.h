#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "DoorEvent.generated.h"

class UStaticMeshComponent;

//열리는 연출은 블루프린트가 OnOpen 에서 구현하고, 다 열리면 FinishOpen 을 호출한다
UCLASS(Blueprintable)
class ADoorEvent : public AScenePlacedEvent
{
	GENERATED_BODY()

public:
	ADoorEvent();

	//고정된 문틀 메쉬. 배치 후 디테일 패널에서 메쉬를 지정한다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> FrameMesh;

	//FrameMesh의 하위
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	virtual void Execute(const UObject* WorldContext = nullptr) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	//Execute 는 서버에서만 불리므로, 머테리얼/콜리전 변경이 클라이언트에도 보이도록 서버와 모든 클라이언트에서 한 번씩 호출된다
	UFUNCTION(BlueprintImplementableEvent, Category = "Door")
	void OnOpen();

	//PostEvents 는 서버에서만 나간다. 클라이언트의 OnOpen 에서 불려도 무시된다
	UFUNCTION(BlueprintCallable, Category = "Door")
	void FinishOpen();

private:
	UPROPERTY(ReplicatedUsing = OnRep_Opened)
	bool bOpened = false;

	UFUNCTION()
	void OnRep_Opened();
};
