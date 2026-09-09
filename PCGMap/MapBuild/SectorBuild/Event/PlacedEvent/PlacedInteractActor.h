#pragma once

#include "CoreMinimal.h"
#include "../SectorEvent.h"
#include "GameplayTagContainer.h"
#include "GameCore/Interface/Interactable.h"
#include "PlacedInteractActor.generated.h"

class UAnimMontage;
class UInteractTriggerComponent;

//상호작용 키로 PreEvents를 발화시킨다. 탈출 시작, 상인, 보상 상자 등
UCLASS()
class APlacedInteractActor : public AScenePlacedEvent, public IInteractable
{
	GENERATED_BODY()

public:
	APlacedInteractActor();

	//ISectorEvent. 다른 이벤트의 PostEvents에 등록해두면 그 완료 신호로 열린다
	virtual void Execute(const UObject* WorldContext = nullptr) override { ActivateInteract(); }

	//IInteractable. UInteractComponent가 서버에서만 호출한다
	virtual void Execute(APlayerController* PC) override;

	//연출 어빌리티가 끝난 뒤 호출한다. 어빌리티가 없으면 Execute가 바로 부른다
	UFUNCTION(BlueprintCallable, Category = "Sector")
	virtual void CompleteInteract();

	//숨겨둔 상호작용을 연다. WaitEvents를 비워두고 직접 열어도 된다. 서버 전용
	UFUNCTION(BlueprintCallable, Category = "Sector")
	void ActivateInteract();

	//열리는 순간 각 머신에서 호출. 등장 연출을 BP에서 붙인다
	UFUNCTION(BlueprintImplementableEvent, Category = "Sector")
	void OnInteractActivated();

	virtual FText GetInteractText() const override { return InteractText; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//GA_Interact가 읽어가는 연출 값들
	float GetInteractFadeDuration() const { return FadeDuration; }
	float GetInteractHoldTime() const { return HoldTime; }
	UAnimMontage* GetInteractMontage() const { return InteractMontage; }

protected:
	virtual void BeginPlay() override;

	//"상자 열기" 같은 고정 문구. 값이 변하는 문구는 파생 클래스에서 GetInteractText를 오버라이드한다
	UPROPERTY(EditAnywhere, Category = "Sector")
	FText InteractText;

	//끄면 상호작용할 때마다 다시 발화한다. 반복 이용하는 경우에 끈다
	UPROPERTY(EditAnywhere, Category = "Sector")
	bool bOnce = true;

	//끄면 숨은 채로 시작한다. 다른 이벤트가 Execute를 부르면 열린다
	UPROPERTY(EditAnywhere, Category = "Sector")
	bool bStartActive = true;

	//상호작용 시 보낼 게임플레이 이벤트. 비우면 연출 없이 바로 발화한다
	UPROPERTY(EditAnywhere, Category = "Sector")
	FGameplayTag InteractEventTag;

	//연출용 몽타주. 비우면 재생하지 않는다
	UPROPERTY(EditAnywhere, Category = "Sector|Interact")
	TObjectPtr<UAnimMontage> InteractMontage;

	//화면이 완전히 덮이기까지 걸리는 시간. 0이면 페이드하지 않는다
	UPROPERTY(EditAnywhere, Category = "Sector|Interact", meta = (ClampMin = "0.0"))
	float FadeDuration = 0.f;

	//덮인 뒤 이만큼 더 붙잡고 있다가 CompleteInteract를 부른다
	UPROPERTY(EditAnywhere, Category = "Sector|Interact", meta = (ClampMin = "0.0"))
	float HoldTime = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sector")
	TObjectPtr<UInteractTriggerComponent> Trigger;

private:
	UFUNCTION()
	void OnRep_Triggered();

	UFUNCTION()
	void OnRep_Active();

	//UI 표시는 각 머신이 하므로 소진/개방 여부를 복제해야 한다
	UPROPERTY(ReplicatedUsing = OnRep_Triggered)
	bool bTriggered = false;

	UPROPERTY(ReplicatedUsing = OnRep_Active)
	bool bActive = false;
};
