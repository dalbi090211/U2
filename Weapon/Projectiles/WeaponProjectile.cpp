#include "WeaponProjectile.h"
#include "WeaponProjectileImpactComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

#include "ProjectilePoolComponent.h"
#include "SmartTrackingProjectileFlightComponent.h"
#include "../../Character/BaseCharacter.h"
#include "../Core/WeaponBase.h"

#include "Components/SphereComponent.h"
#include "Components/SplineComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayCueManager.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AWeaponProjectile::AWeaponProjectile()
{
	bReplicates = true;
	ImpactComponent = CreateDefaultSubobject<UWeaponProjectileImpactComponent>(TEXT("ImpactComponent"));
	SetReplicateMovement(true);

	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	CollisionComponent =
		CreateDefaultSubobject<USphereComponent>(
			TEXT("CollisionComponent"));

	CollisionComponent->InitSphereRadius(12.0f);

	CollisionComponent->SetCollisionEnabled(
		ECollisionEnabled::QueryOnly);

	CollisionComponent->SetCollisionObjectType(
		ECC_TraceAttack);

	CollisionComponent->SetCollisionResponseToAllChannels(
		ECR_Block);

	CollisionComponent->SetCollisionResponseToChannel(
		ECC_Pawn,
		ECR_Block);

	CollisionComponent->bReturnMaterialOnMove = true;

	CollisionComponent->OnComponentHit.AddDynamic(
		this,
		&AWeaponProjectile::OnProjectileHit);

	SetRootComponent(CollisionComponent);


	ProjectileMeshComponent =
		CreateDefaultSubobject<UStaticMeshComponent>(
			TEXT("ProjectileMesh"));

	ProjectileMeshComponent->SetupAttachment(
		CollisionComponent);


	ProjectileMovement =
		CreateDefaultSubobject<UProjectileMovementComponent>(
			TEXT("ProjectileMovement"));

	ProjectileMovement->UpdatedComponent =
		CollisionComponent;

	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;

	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;

	ProjectileMovement->OnProjectileBounce.AddDynamic(
		this,
		&AWeaponProjectile::OnProjectileBounce);

	SmartTrackingFlight = CreateDefaultSubobject<USmartTrackingProjectileFlightComponent>(TEXT("SmartTrackingFlight"));

	InitialLifeSpan = 0.0f;
}

void AWeaponProjectile::SetOwningPool(
	UProjectilePoolComponent* InOwningPool)
{
	OwningPool = InOwningPool;
}

void AWeaponProjectile::ActivateFromPool(
	UProjectilePoolComponent* InOwningPool,
	const FTransform& SpawnTransform)
{
	OwningPool = InOwningPool;

	ImpactComponent->Reset();
	bIsActiveInPool = true;

	SmartTrackingFlight->StopFlight();
	bIsSmartTrackingMode = false;

	SetActorTransform(SpawnTransform);

	SetActorTickEnabled(false);
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetReplicateMovement(true);

	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionEnabled(
			ECollisionEnabled::QueryOnly);

		CollisionComponent->SetGenerateOverlapEvents(false);
	}

	if (ProjectileMovement)
	{
		ProjectileMovement->UpdatedComponent =
			CollisionComponent;

		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	UpdateSmartTrackingTrailCue();
}

void AWeaponProjectile::DeactivateToPool()
{
	const bool bShouldReturnToPool =
		bIsActiveInPool;

	bIsActiveInPool = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(
			LifeTimerHandle);
	}

	ResetProjectileState();

	SourceWeapon = nullptr;
	InstigatorActor = nullptr;
	SourceASC = nullptr;
	DamageEffectClass = nullptr;
	FeedbackOverride = nullptr;

	DamageOrigin = FVector::ZeroVector;

	ApplyInactivePoolState();

	if (bShouldReturnToPool && OwningPool)
	{
		OwningPool->ReleaseProjectile(this);
	}
}

void AWeaponProjectile::InitializeProjectile(
	AWeaponBase* InSourceWeapon,
	AActor* InInstigatorActor,
	UAbilitySystemComponent* InSourceASC,
	TSubclassOf<UGameplayEffect> InDamageEffectClass,
	float InAbilityLevel,
	const FWeaponRuntimeStats& InRuntimeStats,
	UWeaponDataAsset* InFeedbackOverride)
{
	InitializeSourceData(InSourceWeapon, InInstigatorActor, InSourceASC,
		InDamageEffectClass, InAbilityLevel, InRuntimeStats, GetActorLocation(), InFeedbackOverride);

	if (ProjectileMovement)
	{
		ConfigureMovement();
	}

	if (SourceWeapon && CollisionComponent)
	{
		CollisionComponent->IgnoreActorWhenMoving(
			SourceWeapon,
			true);

		if (bProjectileShouldBounce
			&& SourceWeapon->GetWeaponCategory() == EWeaponCategory::GrenadeLauncher
			&& InstigatorActor)
		{
			CollisionComponent->IgnoreActorWhenMoving(
				InstigatorActor,
				true);
		}
	}

	StartLifeTimer();
}

void AWeaponProjectile::InitializeSmartTrackingProjectile(
	AWeaponBase* InSourceWeapon,
	AActor* InInstigatorActor,
	UAbilitySystemComponent* InSourceASC,
	TSubclassOf<UGameplayEffect> InDamageEffectClass,
	float InAbilityLevel,
	const FWeaponRuntimeStats& InRuntimeStats,
	AActor* InTargetActor,
	USplineComponent* InSharedPath)
{
	if (!InSharedPath)
	{
		DeactivateToPool();
		return;
	}

	const FVector StartLocation = InSharedPath->GetLocationAtDistanceAlongSpline(
		0.0f, ESplineCoordinateSpace::World);

	InitializeSourceData(InSourceWeapon, InInstigatorActor, InSourceASC,
		InDamageEffectClass, InAbilityLevel, InRuntimeStats, StartLocation, nullptr);
	bIsSmartTrackingMode = true;
	SmartTrackingFlight->StartFlight(InSharedPath);

	UpdateSmartTrackingTrailCue();

	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	if (SourceWeapon && CollisionComponent)
	{
		CollisionComponent->IgnoreActorWhenMoving(
			SourceWeapon,
			true);
	}

	StartLifeTimer();

	SetActorLocation(StartLocation);

	SetActorRotation(
		InSharedPath->GetDirectionAtDistanceAlongSpline(
			0.0f, ESplineCoordinateSpace::World).Rotation());

}

void AWeaponProjectile::ConfigureMovement()
{
	if (!ProjectileMovement)
	{
		return;
	}

	ProjectileMovement->InitialSpeed =
		ProjectileSpeed;

	ProjectileMovement->MaxSpeed =
		ProjectileSpeed;

	ProjectileMovement->UpdatedComponent =
		CollisionComponent;

	ProjectileMovement->ProjectileGravityScale =
		!bIsSmartTrackingMode && bProjectileAffectedByGravity
		? ProjectileGravityScale
		: 0.0f;

	ProjectileMovement->bShouldBounce =
		!bIsSmartTrackingMode && bProjectileShouldBounce;

	ProjectileMovement->Bounciness =
		ProjectileBounciness;

	ProjectileMovement->Velocity =
		GetActorForwardVector()
		* ProjectileSpeed;

	ProjectileMovement->Activate(true);
}

void AWeaponProjectile::ResetProjectileState()
{
	ImpactComponent->Reset();

	SmartTrackingFlight->StopFlight();
	bIsSmartTrackingMode = false;

}

void AWeaponProjectile::ApplyInactivePoolState()
{
	UpdateSmartTrackingTrailCue();

	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}

	if (CollisionComponent)
	{
		CollisionComponent->ClearMoveIgnoreActors();

		CollisionComponent->SetCollisionEnabled(
			ECollisionEnabled::NoCollision);
	}

	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	SetReplicateMovement(false);

	SetActorLocation(FVector::ZeroVector);
}

void AWeaponProjectile::BeginPlay()
{
	Super::BeginPlay();
}

void AWeaponProjectile::ResumeLinearFlight(const FVector& Direction)
{
	if (!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
	ConfigureMovement();
}

void AWeaponProjectile::OnProjectileLifeExpired()
{
	if (HasAuthority())
	{
		DeactivateToPool();
	}
}

void AWeaponProjectile::OnRep_PoolActive()
{
	SetActorHiddenInGame(
		!bIsActiveInPool);

	if (bIsActiveInPool)
	{
		UpdateSmartTrackingTrailCue();
		return;
	}

	ResetProjectileState();
	ApplyInactivePoolState();
}

void AWeaponProjectile::OnRep_SmartTrackingMode()
{
	UpdateSmartTrackingTrailCue();
}

void AWeaponProjectile::UpdateSmartTrackingTrailCue()
{
	const bool bShouldActivateTrailCue = bIsActiveInPool && bIsSmartTrackingMode;
	if (bSmartTrackingTrailCueActive == bShouldActivateTrailCue)
	{
		return;
	}

	UGameplayCueManager* CueManager = UAbilitySystemGlobals::Get().GetGameplayCueManager();
	if (!CueManager)
	{
		return;
	}

	FGameplayCueParameters CueParameters;
	CueParameters.Location = GetActorLocation();
	CueParameters.TargetAttachComponent = GetRootComponent();
	CueManager->HandleGameplayCue(
		this,
		GameplayTagSystem::GameplayCue_Weapon_SmartTrackingTrail,
		bShouldActivateTrailCue ? EGameplayCueEvent::OnActive : EGameplayCueEvent::Removed,
		CueParameters);
	bSmartTrackingTrailCueActive = bShouldActivateTrailCue;
}

void AWeaponProjectile::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(
		OutLifetimeProps);

	DOREPLIFETIME(
		AWeaponProjectile,
		SourceWeapon);

	DOREPLIFETIME(
		AWeaponProjectile,
		InstigatorActor);

	DOREPLIFETIME(
		AWeaponProjectile,
		bIsActiveInPool);

	DOREPLIFETIME(
		AWeaponProjectile,
		bIsSmartTrackingMode);

}

bool AWeaponProjectile::IsActiveProjectile() const
{
	return bIsActiveInPool && !ImpactComponent->HasHit();
}

void AWeaponProjectile::InitializeSourceData(
	AWeaponBase* InSourceWeapon, AActor* InInstigatorActor, UAbilitySystemComponent* InSourceASC,
	TSubclassOf<UGameplayEffect> InDamageEffectClass, float InAbilityLevel,
	const FWeaponRuntimeStats& InRuntimeStats, const FVector& InDamageOrigin,
	UWeaponDataAsset* InFeedbackOverride)
{
	SourceWeapon = InSourceWeapon;
	InstigatorActor = InInstigatorActor;
	SourceASC = InSourceASC;
	DamageEffectClass = InDamageEffectClass;
	AbilityLevel = InAbilityLevel;
	RuntimeStats = InRuntimeStats;
	FeedbackOverride = InFeedbackOverride;
	DamageOrigin = InDamageOrigin;
	ImpactComponent->ResetBounceCount();
	SetInstigator(Cast<APawn>(InstigatorActor));
}

FWeaponProjectileImpactContext AWeaponProjectile::MakeImpactContext()
{
	FWeaponProjectileImpactContext Context;
	Context.Projectile = this;
	Context.SourceWeapon = SourceWeapon;
	Context.InstigatorActor = InstigatorActor;
	Context.SourceASC = SourceASC;
	Context.DamageEffectClass = DamageEffectClass;
	Context.FeedbackOverride = FeedbackOverride;
	Context.CollisionComponent = CollisionComponent;
	Context.ProjectileMovement = ProjectileMovement;
	Context.RuntimeStats = &RuntimeStats;
	Context.DamageOrigin = DamageOrigin;
	Context.AbilityLevel = AbilityLevel;
	Context.bProjectileShouldBounce = bProjectileShouldBounce;
	Context.bProjectileExplodeOnImpact = bProjectileExplodeOnImpact;
	Context.MaxProjectileBounces = MaxProjectileBounces;
	Context.ProjectileBounceSpeedMultiplier = ProjectileBounceSpeedMultiplier;
	return Context;
}

void AWeaponProjectile::OnProjectileHit(
	UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	ImpactComponent->HandleHit(Hit, MakeImpactContext());
}

void AWeaponProjectile::OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	ImpactComponent->HandleBounce(ImpactResult, MakeImpactContext());
}

void AWeaponProjectile::StartLifeTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			LifeTimerHandle,
			this,
			&AWeaponProjectile::OnProjectileLifeExpired,
			ProjectileLifeSeconds,
			false);
	}
}
