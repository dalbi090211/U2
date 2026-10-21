#include "WeaponBase.h"
#include "WeaponFireExecutorComponent.h"
#include "../Data/WeaponDataAsset.h"
#include "../Projectiles/ProjectilePoolComponent.h"
#include "../Mods/SmartTracking/SmartTrackingFireController.h"
#include "../Mods/SmartTracking/SmartTrackingPathComponent.h"
#include "../../Attribute/CharacterAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

AWeaponBase::AWeaponBase()
{
	bReplicates = true;
	FireExecutor = CreateDefaultSubobject<UWeaponFireExecutorComponent>(TEXT("FireExecutor"));
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);

	BarrelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BarrelMesh"));
	BarrelMesh->SetupAttachment(WeaponMesh);

	ProjectilePool = CreateDefaultSubobject<UProjectilePoolComponent>(TEXT("ProjectilePool"));
	CreateDefaultSubobject<USmartTrackingPathComponent>(TEXT("SmartTrackingPaths"));
	SmartTrackingFireController = CreateDefaultSubobject<USmartTrackingFireController>(TEXT("SmartTrackingFireController"));
}

void AWeaponBase::BeginPlay()
{
	Super::BeginPlay();
	SetRuntimeStats(RuntimeStats);
	SetAmmo(CurrentAmmo <= 0 ? RuntimeStats.MagazineSize : CurrentAmmo);
}

void AWeaponBase::InitializeFromData(const UWeaponDataAsset* InWeaponData)
{
	if (!InWeaponData)
	{
		return;
	}

	WeaponData = InWeaponData;
	GeneratedData = FGeneratedWeaponData();
	WeaponCategory = InWeaponData->WeaponCategory;
	SetRuntimeStats(InWeaponData->RuntimeStats);
	ApplyWeaponVisuals();

	SetAmmo(RuntimeStats.MagazineSize);
}

void AWeaponBase::InitializeFromGeneratedData(
	const UWeaponDataAsset* InWeaponTemplate,
	const FGeneratedWeaponData& InGeneratedData,
	int32 InCurrentAmmo)
{
	if (!InWeaponTemplate || !InGeneratedData.bIsGeneratedWeapon)
	{
		return;
	}

	WeaponData = InWeaponTemplate;
	GeneratedData = InGeneratedData;
	WeaponCategory = GeneratedData.WeaponCategory;
	SetRuntimeStats(GeneratedData.RuntimeStats);
	ApplyWeaponVisuals();
	SetAmmo(InCurrentAmmo == INDEX_NONE ? RuntimeStats.MagazineSize : InCurrentAmmo);
}

FGeneratedWeaponData AWeaponBase::GetDropData() const
{
	FGeneratedWeaponData DropData = GeneratedData;
	DropData.bIsGeneratedWeapon = true;
	DropData.WeaponCategory = WeaponCategory;
	DropData.RuntimeStats = RuntimeStats;
	DropData.FrameMesh = WeaponMesh->GetStaticMesh();
	DropData.BarrelMesh = BarrelMesh->GetStaticMesh();
	DropData.BarrelSocketName = BarrelMesh->GetAttachSocketName();
	DropData.BarrelRelativeTransform = BarrelMesh->GetRelativeTransform();
	return DropData;
}

void AWeaponBase::InitializeFromRuntimeStats(const FWeaponRuntimeStats& InRuntimeStats)
{
	SetRuntimeStats(InRuntimeStats);
	SetAmmo(RuntimeStats.MagazineSize);
}

bool AWeaponBase::ExecuteFire(AActor* InstigatorActor)
{
	return ExecuteFire(InstigatorActor, nullptr, nullptr, 1.0f, false);
}

bool AWeaponBase::ExecuteFire(AActor* InstigatorActor, UAbilitySystemComponent* SourceASC, TSubclassOf<UGameplayEffect> DamageEffectClass, float AbilityLevel, bool bShowDebug)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!CanFire())
	{
		return false;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (CurrentTime - LastFireTime < GetCurrentFireInterval(SourceASC))
	{
		return false;
	}

	const FWeaponFireContext FireContext = MakeFireContext(
		InstigatorActor,
		SourceASC,
		DamageEffectClass,
		AbilityLevel,
		bShowDebug);

	if (!HandleFire(FireContext))
	{
		return false;
	}

	return true;
}

int32 AWeaponBase::ExecuteSmartTrackingFire(
	AActor* InstigatorActor,
	UAbilitySystemComponent* SourceASC,
	TSubclassOf<UGameplayEffect> DamageEffectClass,
	float AbilityLevel,
	const TArray<FSmartTrackingFireRequest>& FireRequests)
{
	return SmartTrackingFireController
		? SmartTrackingFireController->ExecuteFire(
			InstigatorActor,
			SourceASC,
			DamageEffectClass,
			AbilityLevel,
			FireRequests)
		: 0;
}

void AWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelSmartTrackingFire();
	Super::EndPlay(EndPlayReason);
}

void AWeaponBase::CancelSmartTrackingFire()
{
	if (SmartTrackingFireController)
	{
		SmartTrackingFireController->CancelPendingFire();
	}
}
int32 AWeaponBase::SacrificeMagazineAmmo()
{
	if (!HasAuthority())
	{
		return 0;
	}
	const int32 SacrificedAmmo = CurrentAmmo;
	SetAmmo(0);
	return SacrificedAmmo;
}

bool AWeaponBase::ApplyReload()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!CanReload())
	{
		return false;
	}

	SetAmmo(RuntimeStats.MagazineSize);
	return true;
}

bool AWeaponBase::CanFire() const
{
	return CurrentAmmo > 0
		&& (!SmartTrackingFireController || !SmartTrackingFireController->IsBusy());
}

bool AWeaponBase::CanReload() const
{
	return (!SmartTrackingFireController || !SmartTrackingFireController->IsBusy())
		&& CurrentAmmo < RuntimeStats.MagazineSize;
}

FTransform AWeaponBase::GetFireTransform(const AActor* InstigatorActor) const
{
	UStaticMeshComponent* FirePointMesh = GetFirePointMeshComponent();
	if (FirePointMesh)
	{
		return FirePointMesh->GetSocketTransform(FirePointSocketName, RTS_World);
	}

	if (InstigatorActor)
	{
		return InstigatorActor->GetActorTransform();
	}

	return GetActorTransform();
}

FVector AWeaponBase::GetAimDirection(AActor* InstigatorActor, const UAbilitySystemComponent* SourceASC) const
{
	return FireExecutor->GetAimDirection(InstigatorActor);
}

bool AWeaponBase::HandleFire(const FWeaponFireContext& FireContext)
{
	return FireExecutor->ExecuteFire(FireContext, ProjectilePool);
}

FWeaponFireContext AWeaponBase::MakeFireContext(
	AActor* InstigatorActor, UAbilitySystemComponent* SourceASC,
	TSubclassOf<UGameplayEffect> DamageEffectClass, float AbilityLevel, bool bShowDebug) const
{
	return FireExecutor->MakeFireContext(InstigatorActor, SourceASC, DamageEffectClass, AbilityLevel, bShowDebug);
}

bool AWeaponBase::SpawnSmartTrackingProjectile(
	const FWeaponFireContext& FireContext, AActor* TargetActor, USplineComponent* SharedPath)
{
	return FireExecutor->SpawnSmartTrackingProjectile(FireContext, TargetActor, SharedPath, ProjectilePool);
}

void AWeaponBase::SetRuntimeStats(const FWeaponRuntimeStats& InRuntimeStats)
{
	const bool bModifiersChanged = RuntimeStats.Modifiers != InRuntimeStats.Modifiers;
	RuntimeStats = InRuntimeStats;
	RuntimeStats.Sanitize();
	SetAmmo(CurrentAmmo);
	if (bModifiersChanged)
	{
		OnModifiersChanged.Broadcast();
	}
}

void AWeaponBase::SetAmmo(int32 NewCurrentAmmo)
{
	const int32 ClampedCurrent = FMath::Clamp(NewCurrentAmmo, 0, RuntimeStats.MagazineSize);

	if (ClampedCurrent == CurrentAmmo)
	{
		return;
	}

	CurrentAmmo = ClampedCurrent;

	// 서버(및 스탠드얼론)용. 클라이언트는 OnRep_Ammo가 대신 쏜다.
	OnAmmoChanged.Broadcast(CurrentAmmo);
}

void AWeaponBase::OnRep_Ammo()
{
	OnAmmoChanged.Broadcast(CurrentAmmo);
}

float AWeaponBase::GetCurrentFireInterval(UAbilitySystemComponent* SourceASC) const
{
	const float FireIntervalMultiplier = SourceASC
		? SourceASC->GetNumericAttribute(UCharacterAttributeSet::GetFireIntervalMultiplierAttribute())
		: 1.0f;
	return FMath::Max(RuntimeStats.FireInterval * FireIntervalMultiplier, 0.01f);
}

FVector AWeaponBase::GetPreviewFireDirection(AActor* InstigatorActor) const
{
	return GetAimDirection(InstigatorActor);
}

bool AWeaponBase::HasFirePointSocket() const
{
	return GetFirePointMeshComponent() != nullptr;
}

bool AWeaponBase::HasShellEjectSocket() const
{
	return WeaponMesh && WeaponMesh->DoesSocketExist(ShellEjectSocketName);
}

USceneComponent* AWeaponBase::GetFireCueAttachComponent() const
{
	return GetFirePointMeshComponent();
}

void AWeaponBase::PlayReloadSound(EWeaponReloadSoundPhase Phase) const
{
	if (!WeaponData)
	{
		return;
	}

	USoundBase* ReloadSound = nullptr;
	float VolumeMultiplier = 1.0f;
	float PitchMultiplier = 1.0f;
	switch (Phase)
	{
	case EWeaponReloadSoundPhase::Start:
		ReloadSound = WeaponData->ReloadStartSound;
		VolumeMultiplier = WeaponData->ReloadStartVolume;
		PitchMultiplier = WeaponData->ReloadStartPitch;
		break;
	case EWeaponReloadSoundPhase::Insert:
		ReloadSound = WeaponData->ReloadInsertSound;
		VolumeMultiplier = WeaponData->ReloadInsertVolume;
		PitchMultiplier = WeaponData->ReloadInsertPitch;
		break;
	case EWeaponReloadSoundPhase::Complete:
		ReloadSound = WeaponData->ReloadCompleteSound;
		VolumeMultiplier = WeaponData->ReloadCompleteVolume;
		PitchMultiplier = WeaponData->ReloadCompletePitch;
		break;
	default:
		break;
	}

	if (!ReloadSound)
	{
		return;
	}

	USceneComponent* AttachComponent = GetFireCueAttachComponent();
	if (!AttachComponent)
	{
		AttachComponent = GetRootComponent();
	}

	if (AttachComponent)
	{
		UGameplayStatics::SpawnSoundAttached(
			ReloadSound,
			AttachComponent,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			true,
			VolumeMultiplier,
			PitchMultiplier
		);
	}
}

void AWeaponBase::OnRep_WeaponData()
{
	if (WeaponData && !GeneratedData.bIsGeneratedWeapon)
	{
		WeaponCategory = WeaponData->WeaponCategory;
		SetRuntimeStats(WeaponData->RuntimeStats);
	}

	ApplyWeaponVisuals();
}

void AWeaponBase::OnRep_GeneratedData()
{
	if (GeneratedData.bIsGeneratedWeapon)
	{
		WeaponCategory = GeneratedData.WeaponCategory;
		SetRuntimeStats(GeneratedData.RuntimeStats);
	}

	ApplyWeaponVisuals();
}

void AWeaponBase::ApplyWeaponVisuals()
{
	if (!WeaponMesh || !BarrelMesh)
	{
		return;
	}

	UStaticMesh* FrameMesh = GeneratedData.bIsGeneratedWeapon
		? GeneratedData.FrameMesh.Get()
		: WeaponData ? WeaponData->AssemblyData.FrameMesh.Get() : nullptr;
	UStaticMesh* GeneratedBarrelMesh = GeneratedData.bIsGeneratedWeapon
		? GeneratedData.BarrelMesh.Get()
		: WeaponData ? WeaponData->AssemblyData.BarrelMesh.Get() : nullptr;
	const FName BarrelSocketName = GeneratedData.bIsGeneratedWeapon
		? GeneratedData.BarrelSocketName
		: WeaponData ? WeaponData->AssemblyData.BarrelSocketName : TEXT("BarrelSocket");
	const FTransform BarrelRelativeTransform = GeneratedData.bIsGeneratedWeapon
		? GeneratedData.BarrelRelativeTransform
		: WeaponData ? WeaponData->AssemblyData.BarrelRelativeTransform : FTransform::Identity;

	WeaponMesh->SetStaticMesh(FrameMesh);
	BarrelMesh->SetStaticMesh(GeneratedBarrelMesh);
	BarrelMesh->AttachToComponent(
		WeaponMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		BarrelSocketName
	);

	if (FrameMesh && GeneratedBarrelMesh)
	{
		BarrelMesh->SetRelativeTransform(BarrelRelativeTransform);
	}
}

UStaticMeshComponent* AWeaponBase::GetFirePointMeshComponent() const
{
	if (BarrelMesh && BarrelMesh->GetStaticMesh() && BarrelMesh->DoesSocketExist(FirePointSocketName))
	{
		return BarrelMesh.Get();
	}

	if (WeaponMesh && WeaponMesh->DoesSocketExist(FirePointSocketName))
	{
		return WeaponMesh.Get();
	}

	return nullptr;
}

void AWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeaponBase, WeaponData);
	DOREPLIFETIME(AWeaponBase, GeneratedData);
	DOREPLIFETIME(AWeaponBase, CurrentAmmo);
}

void AWeaponBase::CommitFiredShot(const FWeaponFireContext& Context)
{
	if (!HasAuthority() || CurrentAmmo <= 0)
	{
		return;
	}
	LastFireTime = GetWorld() ? GetWorld()->GetTimeSeconds() : LastFireTime;
	SetAmmo(CurrentAmmo - 1);
}
