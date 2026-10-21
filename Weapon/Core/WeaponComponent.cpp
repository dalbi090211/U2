#include "WeaponComponent.h"
#include "WeaponPresentationComponent.h"
#include "Engine/World.h"

#include "WeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "../Mods/SmartTracking/SmartTrackingSessionComponent.h"
#include "../Data/WeaponDataAsset.h"
#include "../Mods/SmartTracking/SmartTrackingTargetingComponent.h"
#include "../../Tags/GameplayTagSystem.h"
#include "../../Ability/GA_Aim.h"
#include "../../Ability/GA_Fire.h"
#include "../../Ability/GA_Reload.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

UWeaponComponent::UWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	Presentation = CreateDefaultSubobject<UWeaponPresentationComponent>(TEXT("WeaponPresentation"));
	SmartTrackingTargetingComponent = CreateDefaultSubobject<USmartTrackingTargetingComponent>(
		TEXT("SmartTrackingTargetingComponent"));
}

void UWeaponComponent::SendConfirmedWeaponCue(
	const FGameplayTag& CueTag, const FGameplayCueParameters& Parameters)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	MulticastConfirmedWeaponCue(++ConfirmedCueSequence, GetWorld()->GetTimeSeconds(),
		CueTag, Parameters, Presentation->ResolveMontage(CueTag));
}

void UWeaponComponent::MulticastConfirmedWeaponCue_Implementation(
	uint32 Sequence, double ServerTime, FGameplayTag CueTag,
	FGameplayCueParameters Parameters, UAnimMontage* Montage)
{
	if (static_cast<int32>(Sequence - LastReceivedCueSequence) <= 0)
	{
		return;
	}
	LastReceivedCueSequence = Sequence;
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState && GameState->GetServerWorldTimeSeconds() - ServerTime > MaxConfirmedCueAge)
	{
		return;
	}

	Presentation->PlayConfirmedCue(CueTag, Parameters, Montage, EquippedWeapon);
}

void UWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	Presentation->Initialize(this);

	if (bAutoEquipDefaultWeapon && GetOwner() && GetOwner()->HasAuthority())
	{
		SpawnAndEquipDefaultWeapon();
	}
}

void UWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UWeaponComponent, EquippedWeapon);
}

AWeaponBase* UWeaponComponent::SpawnAndEquipDefaultWeapon()
{
	return SpawnAndEquipWeapon(DefaultWeaponClass, DefaultWeaponData);
}

AWeaponBase* UWeaponComponent::SpawnAndEquipWeapon(
	TSubclassOf<AWeaponBase> WeaponClass,
	const UWeaponDataAsset* WeaponData)
{
	AWeaponBase* SpawnedWeapon = SpawnWeaponActor(WeaponClass);
	if (!SpawnedWeapon || !WeaponData)
	{
		if (SpawnedWeapon)
		{
			SpawnedWeapon->Destroy();
		}
		return nullptr;
	}

	SpawnedWeapon->InitializeFromData(WeaponData);
	return EquipSpawnedWeapon(SpawnedWeapon);
}

AWeaponBase* UWeaponComponent::SpawnAndEquipGeneratedWeapon(
	TSubclassOf<AWeaponBase> WeaponClass,
	const UWeaponDataAsset* WeaponTemplate,
	const FGeneratedWeaponData& GeneratedData,
	int32 CurrentAmmo)
{
	AWeaponBase* SpawnedWeapon = SpawnWeaponActor(WeaponClass);
	if (!SpawnedWeapon || !WeaponTemplate || !GeneratedData.bIsGeneratedWeapon)
	{
		if (SpawnedWeapon)
		{
			SpawnedWeapon->Destroy();
		}
		return nullptr;
	}

	SpawnedWeapon->InitializeFromGeneratedData(WeaponTemplate, GeneratedData, CurrentAmmo);
	return EquipSpawnedWeapon(SpawnedWeapon);
}

AWeaponBase* UWeaponComponent::SpawnWeaponActor(TSubclassOf<AWeaponBase> WeaponClass) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !WeaponClass)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerActor;
	SpawnParams.Instigator = Cast<APawn>(OwnerActor);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return OwnerActor->GetWorld()->SpawnActor<AWeaponBase>(
		WeaponClass,
		OwnerActor->GetActorTransform(),
		SpawnParams);
}

AWeaponBase* UWeaponComponent::EquipSpawnedWeapon(AWeaponBase* SpawnedWeapon)
{
	if (!SpawnedWeapon)
	{
		return nullptr;
	}

	if (!EquipWeapon(SpawnedWeapon))
	{
		SpawnedWeapon->Destroy();
		return nullptr;
	}

	return SpawnedWeapon;
}

bool UWeaponComponent::CanEquipWeapon(const AWeaponBase* Weapon) const
{
	if (!Weapon)
	{
		return true;
	}

	return AllowedWeaponCategories.Num() == 0
		|| AllowedWeaponCategories.Contains(Weapon->GetWeaponCategory());
}

bool UWeaponComponent::EquipWeapon(AWeaponBase* NewWeapon)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanEquipWeapon(NewWeapon))
	{
		return false;
	}

	if (EquippedWeapon == NewWeapon)
	{
		return true;
	}

	if (EquippedWeapon && EquippedWeapon != NewWeapon)
	{
		UnequipWeapon(true);
	}

	EquippedWeapon = NewWeapon;

	// 서버(및 스탠드얼론)용. 클라이언트는 OnRep_EquippedWeapon이 대신 쏜다.
	if (!EquippedWeapon)
	{
		return true;
	}

	EquippedWeapon->SetOwner(GetOwner());

	if (ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		EquippedWeapon->AttachToComponent(
			OwnerCharacter->GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			AttachSocketName
		);
		EquippedWeapon->SetActorRelativeTransform(WeaponAttachOffset);
	}
	else if (AActor* OwnerActor = GetOwner())
	{
		EquippedWeapon->AttachToActor(
			OwnerActor,
			FAttachmentTransformRules::KeepRelativeTransform
		);
		EquippedWeapon->SetActorRelativeTransform(WeaponAttachOffset);
	}

	Presentation->PlayEquipSound(EquippedWeapon);

	OnWeaponEquipped.Broadcast(EquippedWeapon);
	return true;
}

void UWeaponComponent::UnequipWeapon(bool bDestroyWeapon)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	CancelWeaponActions();
	ClearSmartTrackingLocks();
	AWeaponBase* PreviousWeapon = EquippedWeapon;
	EquippedWeapon = nullptr;
	FinishSmartTrackingVolley();

	if (!PreviousWeapon)
	{
		OnWeaponEquipped.Broadcast(nullptr);
		return;
	}

	PreviousWeapon->CancelSmartTrackingFire();
	PreviousWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PreviousWeapon->SetOwner(nullptr);
	OnWeaponEquipped.Broadcast(nullptr);

	if (bDestroyWeapon)
	{
		PreviousWeapon->Destroy();
	}
}

bool UWeaponComponent::CanFire() const
{
	return !IsSmartTrackingVolleyActive() && EquippedWeapon && EquippedWeapon->CanFire();
}

bool UWeaponComponent::CanReload() const
{
	return !IsSmartTrackingVolleyActive() && EquippedWeapon && EquippedWeapon->CanReload();
}

bool UWeaponComponent::HasModifier(EWeaponModiferType Modifier) const
{
	return EquippedWeapon && EquippedWeapon->HasModifier(Modifier);
}

bool UWeaponComponent::ExecuteFire()
{
	return ExecuteFire(nullptr, nullptr, 1.0f);
}

bool UWeaponComponent::ExecuteFire(UAbilitySystemComponent* SourceASC, TSubclassOf<UGameplayEffect> DamageEffectClass, float AbilityLevel)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	if (!EquippedWeapon)
	{
		return false;
	}

	if (!CanFire())
	{
		return false;
	}

	const TSubclassOf<UGameplayEffect> SharedDamageEffect = GetFireDamageEffectClass();
	return EquippedWeapon->ExecuteFire(GetOwner(), SourceASC, SharedDamageEffect ? SharedDamageEffect : DamageEffectClass, AbilityLevel, bShowDebug);
}

bool UWeaponComponent::IsWeaponCategoryAllowed(EWeaponCategory Category) const
{
	return AllowedWeaponCategories.Num() == 0 || AllowedWeaponCategories.Contains(Category);
}

void UWeaponComponent::BroadcastSmartTrackingShotFired(
	const FSmartTrackingShotResult& Result)
{
	OnSmartTrackingShotFired.Broadcast(Result);
}

TSubclassOf<UGameplayEffect> UWeaponComponent::GetFireDamageEffectClass() const
{
	const UWeaponDataAsset* Data = EquippedWeapon ? EquippedWeapon->GetWeaponData() : nullptr;
	if (Data && Data->DamageEffectClass)
	{
		return Data->DamageEffectClass;
	}
	// Existing GA blueprints publish their configuration on grant.
	// Conflicting definitions require an explicit weapon asset setting.
	TSubclassOf<UGameplayEffect> Resolved;
	for (const auto& Pair : GrantedFireDamageEffects)
	{
		if (Resolved && Resolved != Pair.Value)
		{
			return nullptr;
		}
		Resolved = Pair.Value;
	}
	return Resolved;
}

void UWeaponComponent::RegisterFireDamageEffect(FGameplayAbilitySpecHandle Handle, TSubclassOf<UGameplayEffect> EffectClass)
{
	if (EffectClass)
	{
		GrantedFireDamageEffects.Add(Handle, EffectClass);
	}
}

void UWeaponComponent::UnregisterFireDamageEffect(FGameplayAbilitySpecHandle Handle)
{
	GrantedFireDamageEffects.Remove(Handle);
}

void UWeaponComponent::CancelWeaponActions()
{
	if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
	{
		FGameplayTagContainer Tags;
		Tags.AddTag(GameplayTagSystem::Ability_Weapon_Fire);
		Tags.AddTag(GameplayTagSystem::Ability_Weapon_Reload);
		Tags.AddTag(GameplayTagSystem::Ability_Weapon_Aim);

		// Blueprint defaults can override asset tags. Native weapon actions must
		// still be cancelled when their weapon is removed or replaced.
		TArray<FGameplayAbilitySpecHandle> ActionsToCancel;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UGameplayAbility* Ability = Spec.Ability;
			if (Spec.IsActive() && Ability
				&& (Ability->GetAssetTags().HasAny(Tags)
					|| Ability->IsA<UGA_Fire>()
					|| Ability->IsA<UGA_Reload>()
					|| Ability->IsA<UGA_Aim>()))
			{
				ActionsToCancel.Add(Spec.Handle);
			}
		}

		// Cancellation callbacks can modify the ability list.
		for (const FGameplayAbilitySpecHandle Handle : ActionsToCancel)
		{
			ASC->CancelAbilityHandle(Handle);
		}
	}
}

bool UWeaponComponent::ApplyReload()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	return CanReload() ? EquippedWeapon->ApplyReload() : false;
}

float UWeaponComponent::GetReloadTime() const
{
	return EquippedWeapon ? EquippedWeapon->GetReloadTime() : 0.0f;
}

void UWeaponComponent::OnRep_EquippedWeapon()
{
	CancelWeaponActions();
	Presentation->PlayEquipSound(EquippedWeapon);
	OnWeaponEquipped.Broadcast(EquippedWeapon);
}

USmartTrackingSessionComponent* UWeaponComponent::GetSmartTrackingSession() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<USmartTrackingSessionComponent>() : nullptr;
}

bool UWeaponComponent::IsSmartTrackingVolleyActive() const
{
	const USmartTrackingSessionComponent* Session = GetSmartTrackingSession();
	return Session && Session->IsVolleyActive();
}

void UWeaponComponent::RequestSmartTrackingFire(float AbilityLevel, const TArray<FSmartTrackingFireRequest>& Requests)
{
	if (USmartTrackingSessionComponent* Session = GetSmartTrackingSession())
	{
		Session->RequestSmartTrackingFire(AbilityLevel, Requests);
	}
}

void UWeaponComponent::BeginSmartTrackingLock(AActor* TargetActor)
{
	if (USmartTrackingSessionComponent* Session = GetSmartTrackingSession())
	{
		Session->BeginSmartTrackingLock(TargetActor);
	}
}

void UWeaponComponent::EndSmartTrackingLock(AActor* TargetActor)
{
	if (USmartTrackingSessionComponent* Session = GetSmartTrackingSession())
	{
		Session->EndSmartTrackingLock(TargetActor);
	}
}

void UWeaponComponent::ClearSmartTrackingLocks()
{
	if (USmartTrackingSessionComponent* Session = GetSmartTrackingSession())
	{
		Session->ClearSmartTrackingLocks();
	}
}

void UWeaponComponent::FinishSmartTrackingVolley()
{
	if (USmartTrackingSessionComponent* Session = GetSmartTrackingSession())
	{
		Session->FinishSmartTrackingVolley();
	}
}
