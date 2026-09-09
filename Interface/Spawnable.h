#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Spawnable.generated.h"

UINTERFACE(MinimalAPI)
class USpawnable : public UInterface
{
	GENERATED_BODY()
};

class ISpawnable
{
	GENERATED_BODY()

public:
	virtual void Spawn() = 0;
	virtual void Hide() = 0;

private:
	// virtual UAnimMontage* GetSpawnMontage() const = 0;	//언리얼 구조 문제 때문에 인터페이스에 uproperty로 몽타주 주입 못함, 따라서 상속받는 클래스에 이관함
	// virtual UAnimInstance* GetSpawnAnimInstance() const = 0;
};