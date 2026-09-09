#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractComponent.generated.h"

class UInputAction;
class UInputComponent;

// 플레이어에 부착하는 Invoker.
// 범위 안에 들어온 IInteractable 후보를 모아두고, 액션 키를 누른 시점에
// 가장 가까운 하나의 Execute() 만 실행한다.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_FIN_API UInteractComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractComponent();

	// APlayerCharacter::SetupPlayerInputComponent 에서 호출.
	void BindInput(UInputComponent* PlayerInputComponent, UInputAction* InteractAction);

	// 트리거 진입/이탈 시 상호작용 액터가 직접 호출한다.
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void Register(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Interact")
	void Unregister(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Interact")
	AActor* GetNearest() const;
	
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void Invoke();

	//대상의 문구가 바뀌었을 때 호출. 결과는 UEventManager로 흘린다
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void RefreshPrompt();

private:
	// 대상은 서버가 자기 Candidates 에서 고른다
	UFUNCTION(Server, Reliable)
	void Server_Invoke();

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Candidates;
};
