#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CharacterInitData.generated.h"

// 캐릭터 하나의 시작 어트리뷰트 값. DataTable의 행 타입으로 쓴다.
// UGE_InitAttributes가 이 값으로 어트리뷰트 베이스를 덮어쓴다.
USTRUCT(BlueprintType)
struct FCharacterInitStats : public FTableRowBase
{
	GENERATED_BODY()

	// Health는 스폰 시 MaxHealth로 가득 채워진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	// Stamina도 스폰 시 MaxStamina로 가득 채워진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "1.0"))
	float MaxStamina = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float StaminaRegenRate = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float MovementSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float Armor = 0.0f;

	// 공격력 배수 (1.0 = 100%)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float AttackPower = 1.0f;
};
