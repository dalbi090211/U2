#include "WeaponCueLibrary.h"

#include "../Core/WeaponBase.h"
#include "../Core/WeaponComponent.h"
#include "../Data/WeaponDataAsset.h"
#include "../../Tags/GameplayTagSystem.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayPrediction.h"

namespace
{
	UAbilitySystemComponent* ResolveCueASC(AActor* InstigatorActor, UAbilitySystemComponent* SourceASC)
	{
		return SourceASC ? SourceASC : UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InstigatorActor);
	}

	void DispatchConfirmedCue(AActor* InstigatorActor, UAbilitySystemComponent* ASC,
		const FGameplayTag& Tag, const FGameplayCueParameters& Parameters)
	{
		if (!InstigatorActor || !InstigatorActor->HasAuthority())
		{
			return;
		}
		if (UWeaponComponent* Component = InstigatorActor->FindComponentByClass<UWeaponComponent>())
		{
			Component->SendConfirmedWeaponCue(Tag, Parameters);
		}
		else if (ASC)
		{
			// Non-player weapon sources retain GAS delivery, without an unplayed client prediction.
			FScopedPredictionWindow ConfirmedCue(ASC, FPredictionKey(), false);
			ASC->ExecuteGameplayCue(Tag, Parameters);
		}
	}
}

namespace WeaponCueLibrary
{
	FWeaponCueContext ResolveWeaponCueContext(const FGameplayCueParameters& Parameters)
	{
		FWeaponCueContext Context;
		Context.Weapon = Cast<AWeaponBase>(Parameters.EffectCauser.Get());

		const UWeaponDataAsset* WeaponData = Cast<const UWeaponDataAsset>(Parameters.SourceObject.Get());
		if (!WeaponData && Context.Weapon)
		{
			WeaponData = Context.Weapon->GetWeaponData();
		}

		Context.FeedbackSet = WeaponData
			? WeaponData->FindFeedbackSet(Context.Weapon ? Context.Weapon->GetWeaponCategory() : WeaponData->WeaponCategory)
			: nullptr;

		return Context;
	}

	FGameplayCueParameters MakeWeaponCueParameters(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		const FVector& Location,
		const FVector& Normal)
	{
		FGameplayCueParameters CueParameters;
		// 큐 규칙: EffectCauser는 무기 액터, SourceObject는 무기 데이터,
		// TargetAttachComponent는 총구 연출용 FirePointSocket을 가진 컴포넌트다.
		CueParameters.Location = Location;
		CueParameters.Normal = Normal;
		CueParameters.Instigator = InstigatorActor;
		CueParameters.EffectCauser = Weapon;
		CueParameters.SourceObject = Weapon ? Weapon->GetWeaponData() : nullptr;
		CueParameters.TargetAttachComponent = Weapon ? Weapon->GetFireCueAttachComponent() : nullptr;
		return CueParameters;
	}

	FGameplayCueParameters MakeTraceCueParameters(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		const FTransform& FireTransform,
		const FVector& TraceEnd)
	{
		const FVector TraceOffset = TraceEnd - FireTransform.GetLocation();
		FGameplayCueParameters CueParameters = MakeWeaponCueParameters(
			Weapon,
			InstigatorActor,
			FireTransform.GetLocation(),
			TraceOffset.GetSafeNormal()
		);
		CueParameters.RawMagnitude = TraceOffset.Length();
		return CueParameters;
	}

	void ExecuteFireCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FTransform& FireTransform)
	{
		UAbilitySystemComponent* CueASC = ResolveCueASC(InstigatorActor, SourceASC);
		FGameplayCueParameters CueParameters = MakeWeaponCueParameters(
			Weapon,
			InstigatorActor,
			FireTransform.GetLocation(),
			FireTransform.GetRotation().GetForwardVector()
		);

		const FGameplayTag CueTag = GameplayTagSystem::GameplayCue_Weapon_Fire;
		DispatchConfirmedCue(InstigatorActor, CueASC, CueTag, CueParameters);
	}

	void ExecuteSmartTrackingFireCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FTransform& FireTransform)
	{
		UAbilitySystemComponent* CueASC = ResolveCueASC(InstigatorActor, SourceASC);
		FGameplayCueParameters CueParameters = MakeWeaponCueParameters(
			Weapon, InstigatorActor, FireTransform.GetLocation(),
			FireTransform.GetRotation().GetForwardVector());
		CueParameters.RawMagnitude = -1.0f;
		DispatchConfirmedCue(InstigatorActor, CueASC, GameplayTagSystem::GameplayCue_Weapon_Fire, CueParameters);
	}

	void ExecuteFireCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FTransform& FireTransform,
		const FVector& TraceEnd)
	{
		UAbilitySystemComponent* CueASC = ResolveCueASC(InstigatorActor, SourceASC);
		FGameplayCueParameters CueParameters = MakeTraceCueParameters(Weapon, InstigatorActor, FireTransform, TraceEnd);

		const FGameplayTag CueTag = GameplayTagSystem::GameplayCue_Weapon_Fire;
		DispatchConfirmedCue(InstigatorActor, CueASC, CueTag, CueParameters);
	}

	void ExecuteImpactCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FHitResult& Hit,
		bool bHitCharacter,
		UWeaponDataAsset* FeedbackOverride,
		float ExplosionRadiusOverride)
	{
		UAbilitySystemComponent* CueASC = ResolveCueASC(InstigatorActor, SourceASC);
		FGameplayCueParameters CueParameters = MakeWeaponCueParameters(Weapon, InstigatorActor, Hit.ImpactPoint, Hit.ImpactNormal);
		CueParameters.PhysicalMaterial = Hit.PhysMaterial.Get();
		CueParameters.SourceObject = FeedbackOverride ? FeedbackOverride : CueParameters.SourceObject.Get();
		CueParameters.RawMagnitude = ExplosionRadiusOverride >= 0.0f
			? ExplosionRadiusOverride : (Weapon ? Weapon->GetRuntimeStats().ExplosionRadius : 0.0f);

		const FGameplayTag CueTag = bHitCharacter
			? GameplayTagSystem::GameplayCue_Weapon_Impact_Character
			: GameplayTagSystem::GameplayCue_Weapon_Impact_Surface;

		DispatchConfirmedCue(InstigatorActor, CueASC, CueTag, CueParameters);
	}

	void ExecuteBounceCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FHitResult& Hit,
		UWeaponDataAsset* FeedbackOverride)
	{
		UAbilitySystemComponent* CueASC = ResolveCueASC(InstigatorActor, SourceASC);
		FGameplayCueParameters CueParameters = MakeWeaponCueParameters(Weapon, InstigatorActor, Hit.ImpactPoint, Hit.ImpactNormal);
		CueParameters.PhysicalMaterial = Hit.PhysMaterial.Get();
		CueParameters.SourceObject = FeedbackOverride ? FeedbackOverride : CueParameters.SourceObject.Get();
		CueParameters.RawMagnitude = -1.0f;

		DispatchConfirmedCue(InstigatorActor, CueASC, GameplayTagSystem::GameplayCue_Weapon_Impact_Surface, CueParameters);
	}
}
