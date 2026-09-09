#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(MinimalAPI)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

class IInteractable
{
	GENERATED_BODY()

public:
	/*
	 * PC 는 상호작용한 플레이어
	 */
	virtual void Execute(APlayerController *PC) = 0;

	/*
	 * 범위 안에 있을 때 표시할 안내 문구. 빈 텍스트면 표시하지 않는다.
	 * 값이 상황에 따라 변하면(가격 인상 등) 그때그때 계산해서 반환하면 된다
	 */
	virtual FText GetInteractText() const { return FText::GetEmpty(); }
};
