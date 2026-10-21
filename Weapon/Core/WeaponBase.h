#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Data/WeaponDataAsset.h"
#include "../Data/WeaponTypes.h"
#include "WeaponBase.generated.h"

class AActor;
class UAbilitySystemComponent;
class UGameplayEffect;
class UProjectilePoolComponent;
class USceneComponent;
class USplineComponent;
class UStaticMeshComponent;
class USmartTrackingFireController;
class UWeaponFireExecutorComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAmmoChangedDelegate, int32, CurrentAmmo);

USTRUCT()
struct TEAMPROJECT_FIN_API FWeaponFireContext
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<AActor> InstigatorActor;
	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> SourceASC;
	UPROPERTY()
	TSubclassOf<UGameplayEffect> DamageEffectClass;
	float AbilityLevel = 1.0f;
	bool bShowDebug = false;
	FTransform FireTransform = FTransform::Identity;
	FVector FireDirection = FVector::ForwardVector;
};

UCLASS()
class TEAMPROJECT_FIN_API AWeaponBase : public AActor
{
	GENERATED_BODY()
	
public:	
	AWeaponBase();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	void InitializeFromData(const UWeaponDataAsset* InWeaponData);
	void InitializeFromGeneratedData(
		const UWeaponDataAsset* InWeaponTemplate,
		const FGeneratedWeaponData& InGeneratedData,
		int32 InCurrentAmmo = INDEX_NONE);
	FGeneratedWeaponData GetDropData() const;
	void InitializeFromRuntimeStats(const FWeaponRuntimeStats& InRuntimeStats);

	bool ExecuteFire(AActor* InstigatorActor);
	bool ExecuteFire(AActor* InstigatorActor, UAbilitySystemComponent* SourceASC, TSubclassOf<UGameplayEffect> DamageEffectClass, float AbilityLevel = 1.0f, bool bShowDebug = false);
	int32 ExecuteSmartTrackingFire(
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		TSubclassOf<UGameplayEffect> DamageEffectClass,
		float AbilityLevel,
		const TArray<FSmartTrackingFireRequest>& FireRequests);
	void CancelSmartTrackingFire();
	bool ApplyReload();
	// Deployment consumes the current magazine; the normal reload then refills it.
	int32 SacrificeMagazineAmmo();
	// Called after a successful shot by both standard and queued fire paths.
	void CommitFiredShot(const FWeaponFireContext& Context);
	FWeaponFireContext MakeFireContext(AActor* InstigatorActor, UAbilitySystemComponent* SourceASC, TSubclassOf<UGameplayEffect> DamageEffectClass, float AbilityLevel, bool bShowDebug) const;
	bool SpawnSmartTrackingProjectile(
		const FWeaponFireContext& FireContext,
		AActor* TargetActor,
		USplineComponent* SharedPath);


	bool CanFire() const;
	bool CanReload() const;

	EWeaponCategory GetWeaponCategory() const { return WeaponCategory; }
	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	const FWeaponRuntimeStats& GetRuntimeStats() const { return RuntimeStats; }
	const UWeaponDataAsset* GetWeaponData() const { return WeaponData; }
	bool IsGeneratedWeapon() const { return GeneratedData.bIsGeneratedWeapon; }
	bool HasModifier(EWeaponModiferType Modifier) const { return RuntimeStats.Modifiers.Contains(Modifier); }
	// Runtime modifier changes also invalidate passive mod state on the same weapon.
	FSimpleMulticastDelegate OnModifiersChanged;

	FTransform GetFireTransform(const AActor* InstigatorActor = nullptr) const;
	FVector GetAimDirection(AActor* InstigatorActor, const UAbilitySystemComponent* SourceASC = nullptr) const;
	FVector GetPreviewFireDirection(AActor* InstigatorActor) const;
	UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }
	UStaticMeshComponent* GetBarrelMesh() const { return BarrelMesh; }
	FName GetFirePointSocketName() const { return FirePointSocketName; }
	FName GetShellEjectSocketName() const { return ShellEjectSocketName; }
	bool HasFirePointSocket() const;
	bool HasShellEjectSocket() const;
	USceneComponent* GetFireCueAttachComponent() const;
	UStaticMeshComponent* GetFirePointMeshComponent() const;
	void PlayReloadSound(EWeaponReloadSoundPhase Phase) const;

	
	UFUNCTION(BlueprintPure, Category = "Weapon|Ammo")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	float GetReloadTime() const { return RuntimeStats.ReloadTime; }

	// 탄약이 실제로 바뀔 때만 발화. UI는 바인딩 직후 현재값을 한 번 직접 읽어야 한다.
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Ammo")
	FOnAmmoChangedDelegate OnAmmoChanged;

	float GetCurrentFireInterval(UAbilitySystemComponent* SourceASC) const;
protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual bool HandleFire(const FWeaponFireContext& FireContext);

	void SetRuntimeStats(const FWeaponRuntimeStats& InRuntimeStats);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> BarrelMesh;

	// 투사체 기반 무기에서 투사체 액터를 재사용한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Projectile")
	TObjectPtr<UProjectilePoolComponent> ProjectilePool;

	UPROPERTY(VisibleAnywhere, Category = "Weapon|Smart Tracking")
	TObjectPtr<USmartTrackingFireController> SmartTrackingFireController;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FName FirePointSocketName = TEXT("FirePointSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|FX")
	FName ShellEjectSocketName = TEXT("ShellEjectSocket");

	UPROPERTY(ReplicatedUsing = OnRep_WeaponData, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<const UWeaponDataAsset> WeaponData;

	UPROPERTY(ReplicatedUsing = OnRep_GeneratedData, BlueprintReadOnly, Category = "Weapon|Generated")
	FGeneratedWeaponData GeneratedData;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	EWeaponCategory WeaponCategory = EWeaponCategory::Rifle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FWeaponRuntimeStats RuntimeStats;

	UPROPERTY(ReplicatedUsing = OnRep_Ammo, BlueprintReadOnly, Category = "Weapon|Ammo")
	int32 CurrentAmmo = 0;

	// 탄약을 바꾸는 유일한 경로. 여기서만 OnAmmoChanged를 쏜다.
	void SetAmmo(int32 NewCurrentAmmo);

private:
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UWeaponFireExecutorComponent> FireExecutor;

	UFUNCTION()
	void OnRep_WeaponData();

	UFUNCTION()
	void OnRep_GeneratedData();

	UFUNCTION()
	void OnRep_Ammo();

	void ApplyWeaponVisuals();

	double LastFireTime = -9999.0;
};
