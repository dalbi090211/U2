#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayEffectTypes.h"
#include "../Data/WeaponTypes.h"
#include "../Data/SmartTrackingTypes.h"
#include "WeaponComponent.generated.h"

class AWeaponBase;
class UAbilitySystemComponent;
class UGameplayEffect;
class UWeaponDataAsset;
class UAnimMontage;
class UWeaponPresentationComponent;
class USmartTrackingTargetingComponent;
class USmartTrackingSessionComponent;
struct FGameplayEventData;
struct FGameplayTag;

// 장착 무기가 바뀌었다. 해제 시 nullptr이 넘어온다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponEquippedDelegate, AWeaponBase*, NewWeapon);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TEAMPROJECT_FIN_API UWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponComponent();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	AWeaponBase* SpawnAndEquipDefaultWeapon();
	AWeaponBase* SpawnAndEquipWeapon(TSubclassOf<AWeaponBase> WeaponClass, const UWeaponDataAsset* WeaponData);
	AWeaponBase* SpawnAndEquipGeneratedWeapon(
		TSubclassOf<AWeaponBase> WeaponClass,
		const UWeaponDataAsset* WeaponTemplate,
		const FGeneratedWeaponData& GeneratedData,
		int32 CurrentAmmo = INDEX_NONE);

	bool CanEquipWeapon(const AWeaponBase* Weapon) const;
	bool IsWeaponCategoryAllowed(EWeaponCategory Category) const;
	bool EquipWeapon(AWeaponBase* NewWeapon);
	void UnequipWeapon(bool bDestroyWeapon = false);

	bool CanFire() const;
	bool CanReload() const;

	bool ExecuteFire();
	bool ExecuteFire(UAbilitySystemComponent* SourceASC, TSubclassOf<UGameplayEffect> DamageEffectClass, float AbilityLevel = 1.0f);
	void SendConfirmedWeaponCue(const FGameplayTag& CueTag, const FGameplayCueParameters& Parameters);

	// Smart Tracking
	void RequestSmartTrackingFire(
		float AbilityLevel,
		const TArray<FSmartTrackingFireRequest>& FireRequests);
	void BeginSmartTrackingLock(AActor* TargetActor);
	void EndSmartTrackingLock(AActor* TargetActor);
	void ClearSmartTrackingLocks();
	void FinishSmartTrackingVolley();
	bool IsSmartTrackingVolleyActive() const;
	void BroadcastSmartTrackingShotFired(const FSmartTrackingShotResult& Result);
	FOnSmartTrackingShotFired OnSmartTrackingShotFired;

	bool ApplyReload();
	TSubclassOf<UGameplayEffect> GetFireDamageEffectClass() const;
	void RegisterFireDamageEffect(FGameplayAbilitySpecHandle Handle, TSubclassOf<UGameplayEffect> EffectClass);
	void UnregisterFireDamageEffect(FGameplayAbilitySpecHandle Handle);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	AWeaponBase* GetEquippedWeapon() const { return EquippedWeapon; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Modifier")
	bool HasModifier(EWeaponModiferType Modifier) const;

	// Smart Tracking targeting is controlled by the Smart Tracking ability.
	USmartTrackingTargetingComponent* GetSmartTrackingTargetingComponent() const
	{
		return SmartTrackingTargetingComponent;
	}

	// 클라이언트는 무기 복제가 늦게 도착한다. UI는 여기 바인딩하고 현재값도 한 번 읽을 것.
	UPROPERTY(BlueprintAssignable, Category = "Weapon")
	FOnWeaponEquippedDelegate OnWeaponEquipped;
	bool HasWeapon() const { return EquippedWeapon != nullptr; }
	bool ShouldShowDebug() const { return bShowDebug; }

	float GetReloadTime() const;

protected:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastConfirmedWeaponCue(uint32 Sequence, double ServerTime, FGameplayTag CueTag,
		FGameplayCueParameters Parameters, UAnimMontage* Montage);

	// Discard delayed cosmetic events instead of replaying a backlog of shots.
	UPROPERTY(EditDefaultsOnly, Category = "Weapon|Feedback", meta = (ClampMin = "0.0", Units = "s"))
	float MaxConfirmedCueAge = 0.5f;

	uint32 ConfirmedCueSequence = 0;
	uint32 LastReceivedCueSequence = 0;

	UFUNCTION()
	void OnRep_EquippedWeapon();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<AWeaponBase> DefaultWeaponClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<const UWeaponDataAsset> DefaultWeaponData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	bool bAutoEquipDefaultWeapon = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TArray<EWeaponCategory> AllowedWeaponCategories;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FName AttachSocketName = TEXT("WeaponSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FTransform WeaponAttachOffset = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Debug")
	bool bShowDebug = false;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedWeapon, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<AWeaponBase> EquippedWeapon;

	UPROPERTY(VisibleAnywhere, Category = "Weapon|Smart Tracking")
	TObjectPtr<USmartTrackingTargetingComponent> SmartTrackingTargetingComponent;

	USmartTrackingSessionComponent* GetSmartTrackingSession() const;

	UPROPERTY(Transient)
	TMap<FGameplayAbilitySpecHandle, TSubclassOf<UGameplayEffect>> GrantedFireDamageEffects;
	UPROPERTY(VisibleAnywhere, Category = "Weapon|Feedback")
	TObjectPtr<UWeaponPresentationComponent> Presentation;

	void CancelWeaponActions();
	AWeaponBase* SpawnWeaponActor(TSubclassOf<AWeaponBase> WeaponClass) const;
	AWeaponBase* EquipSpawnedWeapon(AWeaponBase* SpawnedWeapon);
};
