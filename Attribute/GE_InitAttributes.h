#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GE_InitAttributes.generated.h"

class UAbilitySystemComponent;
struct FCharacterInitStats;

// FCharacterInitStats 값을 SetByCaller로 받아 어트리뷰트 베이스를 덮어쓰는 즉발 이펙트.
UCLASS()
class TEAMPROJECT_FIN_API UGE_InitAttributes : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_InitAttributes();

	// 시작 스탯을 ASC에 즉시 적용한다. 서버 권한에서만 호출할 것.
	static void ApplyTo(UAbilitySystemComponent* ASC, const FCharacterInitStats& Stats);
};
