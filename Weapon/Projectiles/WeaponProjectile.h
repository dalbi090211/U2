#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Data/WeaponTypes.h"
#include "WeaponProjectile.generated.h"

class AWeaponBase;
class UProjectilePoolComponent;
class UAbilitySystemComponent;
class UGameplayEffect;
class UProjectileMovementComponent;
class USphereComponent;
class USplineComponent;
class UWeaponDataAsset;
class USmartTrackingProjectileFlightComponent;
class UWeaponProjectileImpactComponent;
struct FWeaponProjectileImpactContext;

UCLASS()
class TEAMPROJECT_FIN_API AWeaponProjectile : public AActor
{
	GENERATED_BODY()

public:
	AWeaponProjectile();

	void SetOwningPool(UProjectilePoolComponent* InOwningPool);
	void ActivateFromPool(UProjectilePoolComponent* InOwningPool, const FTransform& SpawnTransform);
	void DeactivateToPool();

	// InFeedbackOverride를 주면 피격/폭발 Cue가 무기 데이터 대신 그 에셋을 쓴다.
	// 무기 스탯과 다른 스탯으로 쏘는 투사체(스킬)용. 생략하면 종전대로 무기 데이터를 쓴다
	void InitializeProjectile(
		AWeaponBase* InSourceWeapon,
		AActor* InInstigatorActor,
		UAbilitySystemComponent* InSourceASC,
	TSubclassOf<UGameplayEffect> InDamageEffectClass,
		float InAbilityLevel,
		const FWeaponRuntimeStats& InRuntimeStats,
		UWeaponDataAsset* InFeedbackOverride = nullptr);

	void InitializeSmartTrackingProjectile(
		AWeaponBase* InSourceWeapon,
		AActor* InInstigatorActor,
		UAbilitySystemComponent* InSourceASC,
		TSubclassOf<UGameplayEffect> InDamageEffectClass,
		float InAbilityLevel,
		const FWeaponRuntimeStats& InRuntimeStats,
		AActor* InTargetActor,
		USplineComponent* InSharedPath);

	bool IsActiveProjectile() const;
	void ResumeLinearFlight(const FVector& Direction);
	float GetProjectileSpeed() const { return ProjectileSpeed; }
	bool IsProjectileAffectedByGravity() const { return bProjectileAffectedByGravity; }
	float GetProjectileGravityScale() const { return ProjectileGravityScale; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnProjectileHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	UFUNCTION()
	void OnProjectileBounce(
		const FHitResult& ImpactResult,
		const FVector& ImpactVelocity);

	void OnProjectileLifeExpired();
	void ConfigureMovement();
	void ResetProjectileState();
	void ApplyInactivePoolState();
	void UpdateSmartTrackingTrailCue();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> ProjectileMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(Replicated)
	TObjectPtr<AWeaponBase> SourceWeapon;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> InstigatorActor;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> SourceASC;

	UPROPERTY()
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	// 서버에서 Cue를 만들 때만 쓴다. 값은 Cue 파라미터로 실려 복제되므로 이 필드는 복제하지 않는다
	UPROPERTY()
	TObjectPtr<UWeaponDataAsset> FeedbackOverride;

	UPROPERTY()
	TObjectPtr<UProjectilePoolComponent> OwningPool;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<USmartTrackingProjectileFlightComponent> SmartTrackingFlight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ProjectileSpeed = 3000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Movement", meta = (ClampMin = "0.1", Units = "s"))
	float ProjectileLifeSeconds = 5.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Movement")
	bool bProjectileAffectedByGravity = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Movement", meta = (EditCondition = "bProjectileAffectedByGravity", ClampMin = "0.0"))
	float ProjectileGravityScale = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Bounce")
	bool bProjectileShouldBounce = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Bounce", meta = (EditCondition = "bProjectileShouldBounce", ClampMin = "0.0"))
	float ProjectileBounciness = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Bounce", meta = (EditCondition = "bProjectileShouldBounce", ClampMin = "0"))
	int32 MaxProjectileBounces = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Bounce", meta = (EditCondition = "bProjectileShouldBounce", ClampMin = "0.0", ClampMax = "1.0"))
	float ProjectileBounceSpeedMultiplier = 0.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Projectile Settings|Impact")
	bool bProjectileExplodeOnImpact = true;

	FTimerHandle LifeTimerHandle;

	FWeaponRuntimeStats RuntimeStats;

	FVector DamageOrigin = FVector::ZeroVector;

	float AbilityLevel = 1.0f;

	bool bSmartTrackingTrailCueActive = false;
	UPROPERTY(ReplicatedUsing = OnRep_SmartTrackingMode)
	bool bIsSmartTrackingMode = false;

	UPROPERTY(ReplicatedUsing = OnRep_PoolActive)
	bool bIsActiveInPool = false;

private:
	void InitializeSourceData(AWeaponBase* InSourceWeapon, AActor* InInstigatorActor,
		UAbilitySystemComponent* InSourceASC, TSubclassOf<UGameplayEffect> InDamageEffectClass,
		float InAbilityLevel, const FWeaponRuntimeStats& InRuntimeStats,
		const FVector& InDamageOrigin, UWeaponDataAsset* InFeedbackOverride);
	void StartLifeTimer();
	FWeaponProjectileImpactContext MakeImpactContext();

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UWeaponProjectileImpactComponent> ImpactComponent;

	UFUNCTION()
	void OnRep_PoolActive();

	UFUNCTION()
	void OnRep_SmartTrackingMode();

};
