#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "InteractTriggerComponent.generated.h"

//범위에 들어온 플레이어의 InteractComponent에 소유 액터를 후보로 등록한다.
//소유 액터가 IInteractable을 구현하지 않으면 등록은 무시된다
UCLASS(ClassGroup = (Interact), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_FIN_API UInteractTriggerComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UInteractTriggerComponent();

	//안내 문구가 바뀌었을 때 소유 액터가 호출한다. 범위 안 플레이어의 표시를 갱신
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void RefreshPrompt();

	//다 쓴 상호작용을 후보에서 빼고 다시 등록되지 않게 막는다
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void DisableInteraction();

private:
	UFUNCTION()
	void OnEnter(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnLeave(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);
};
