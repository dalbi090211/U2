#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "DoorEvent.generated.h"

class UStaticMeshComponent;
class UNiagaraSystem;
class USoundBase;

//Execute 시 문 메쉬를 설정된 높이까지 올리고 사운드/이펙트를 재생한다. 다 올라가면 PostEvents 발화
UCLASS()
class ADoorEvent : public AScenePlacedEvent
{
	GENERATED_BODY()

public:
	ADoorEvent();

	//고정된 문틀 메쉬. 배치 후 디테일 패널에서 메쉬를 지정한다
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> FrameMesh;

	//위로 올라가는 문짝 메쉬. FrameMesh의 하위
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	//위로 올라갈 높이(cm)
	UPROPERTY(EditAnywhere, Category = "Door")
	float RaiseHeight = 300.f;

	//초당 상승 속도(cm/s)
	UPROPERTY(EditAnywhere, Category = "Door")
	float RaiseSpeed = 100.f;

	UPROPERTY(EditAnywhere, Category = "Door")
	TObjectPtr<USoundBase> OpenSound;

	UPROPERTY(EditAnywhere, Category = "Door")
	TObjectPtr<UNiagaraSystem> OpenEffect;

	virtual void Execute(const UObject* WorldContext = nullptr) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	float RaisedAmount = 0.f;
};
