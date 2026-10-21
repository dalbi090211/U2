#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MonsterInitData.generated.h"

// 몬스터 시작 어트리뷰트 DataTable의 행 타입이다.
// 기존 DataTable/Blueprint의 /Script/TeamProject_Fin.MonsterInitStats 참조를 유지한다.
USTRUCT(BlueprintType)
struct FMonsterInitStats : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float MovementSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float Armor = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes", meta = (ClampMin = "0.0"))
	float AttackPower = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes|Combat", meta = (ClampMin = "0.0"))
	float WeakspotMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes|Combat", meta = (ClampMin = "0.0"))
	float StaggerResistance = 1.0f;
};
