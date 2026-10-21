// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"
#include "GameFramework/Character.h"
#include "BaseCharacter.generated.h"

class ABaseCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChangedDelegate, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStaminaChangedDelegate, float, CurrentStamina, float, MaxStamina);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSnowDebuffChangedDelegate, bool, bIsActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDamageTakenDelegate, float, DamageAmount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthCapMultiplierChangedDelegate, float, Multiplier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStaminaCapMultiplierChangedDelegate, float, Multiplier);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDeathStartedDelegate, ABaseCharacter*);

#define ECC_TraceAttack  ECC_GameTraceChannel2
#define ECC_Hitbox       ECC_GameTraceChannel3
#define ECC_EnemyAttackObject ECC_GameTraceChannel4

class UAbilitySystemComponent;
class UAbilitySetDataAsset;
class UCharacterAttributeSet;
class UWeaponComponent;
struct FOnAttributeChangeData;

UCLASS()
class TEAMPROJECT_FIN_API ABaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UFUNCTION(BlueprintPure, Category = "GAS")
	UCharacterAttributeSet* GetCharacterAttributeSet() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UWeaponComponent* GetWeaponComponent() const;

	const FGameplayTag& GetCharacterRoleTag() const { return CharacterRoleTag; }


	virtual void BeginDeath();

	virtual void FinishDeath();

	// 부활 시 상태 및 물리 초기화
	virtual void Revive();

	UPROPERTY(BlueprintAssignable, Category = "GAS|Events")
	FOnHealthChangedDelegate OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "GAS|Events")
	FOnStaminaChangedDelegate OnStaminaChanged;

	UPROPERTY(BlueprintAssignable, Category = "GAS|Events")
	FOnSnowDebuffChangedDelegate OnSnowDebuffChanged;

	UPROPERTY(BlueprintAssignable, Category = "GAS|Events")
	FOnDamageTakenDelegate OnDamageTaken;

	UPROPERTY(BlueprintAssignable, Category = "GAS|Events")
	FOnHealthCapMultiplierChangedDelegate OnHealthCapMultiplierChanged;

	UPROPERTY(BlueprintAssignable, Category = "GAS|Events")
	FOnStaminaCapMultiplierChangedDelegate OnStaminaCapMultiplierChanged;

	// State.Dead 태그 유무와 무관한 실제 사망 시작 통지이다.
	FOnDeathStartedDelegate OnDeathStarted;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Abilities")
	TObjectPtr<UAbilitySetDataAsset> SharedAbilitySet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Abilities")
	TArray<TSubclassOf<class UGameplayAbility>> DefaultAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GAS|Effects")
	TArray<TSubclassOf<class UGameplayEffect>> DefaultEffects;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Attributes",
		meta = (RowType = "/Script/TeamProject_Fin.CharacterInitStats"))
	FDataTableRowHandle InitStatsRow;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS|Tags")
	FGameplayTag CharacterRoleTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCharacterAttributeSet> CharacterAttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWeaponComponent> WeaponComponent;


protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	TObjectPtr<UAnimMontage> DeathMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Death")
	bool bDisableMovementOnDeathStart = false;

public:
	UAnimMontage* GetDeathMontage() const
	{
		return DeathMontage;
	}

protected:

	// State.Dead 태그 콜백과 사망 GA 직접 호출이 겹쳐도 사망 시작 처리는 한 번만 수행한다.
	UPROPERTY(Transient)
	bool bDeathStarted = false;

	UPROPERTY(Transient)
	bool bDeathFinished = false;

public:
	bool HasStartedDeath() const
	{
		return bDeathStarted;
	}

	bool HasFinishedDeath() const
	{
		return bDeathFinished;
	}

private:
	void InitializeAbilitySystem();
	void InitializeASC();
	void ApplyInitialStats();
	void GrantCharacterRoleTag();
	void GrantDefaultAbilities();
	void ApplyDefaultEffects();
	void BroadcastInitialAttributes();
	void BindAttributeDelegates();
	void ApplyMovementSpeed(float NewMovementSpeed);
	void OnMovementSpeedChanged(const FOnAttributeChangeData& Data);
	void OnHealthChangedCallback(const FOnAttributeChangeData& Data);
	void OnStaminaChangedCallback(const FOnAttributeChangeData& Data);
	void OnHealthCapMultiplierChangedCallback(const FOnAttributeChangeData& Data);
	void OnStaminaCapMultiplierChangedCallback(const FOnAttributeChangeData& Data);
	void OnSnowDebuffTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
};
