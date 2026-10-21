#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"

class AWeaponBase;
class UAbilitySystemComponent;
class UWeaponDataAsset;
struct FWeaponFeedbackSet;

struct TEAMPROJECT_FIN_API FWeaponCueContext
{
	const AWeaponBase* Weapon = nullptr;
	const FWeaponFeedbackSet* FeedbackSet = nullptr;

	bool IsValid() const { return Weapon && FeedbackSet; }
};

namespace WeaponCueLibrary
{
	TEAMPROJECT_FIN_API FWeaponCueContext ResolveWeaponCueContext(const FGameplayCueParameters& Parameters);

	TEAMPROJECT_FIN_API FGameplayCueParameters MakeWeaponCueParameters(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		const FVector& Location,
		const FVector& Normal);

	TEAMPROJECT_FIN_API FGameplayCueParameters MakeTraceCueParameters(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		const FTransform& FireTransform,
		const FVector& TraceEnd);

	TEAMPROJECT_FIN_API void ExecuteFireCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FTransform& FireTransform);

	TEAMPROJECT_FIN_API void ExecuteSmartTrackingFireCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FTransform& FireTransform);

	TEAMPROJECT_FIN_API void ExecuteFireCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FTransform& FireTransform,
		const FVector& TraceEnd);

	// FeedbackOverride를 주면 무기 데이터 대신 그 에셋의 피드백 셋을 쓴다.
	// ExplosionRadiusOverride가 0 이상이면 폭발 판정에 무기 스탯 대신 그 값을 쓴다.
	// 둘 다 생략하면 종전대로 무기에서 읽는다.
	TEAMPROJECT_FIN_API void ExecuteImpactCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FHitResult& Hit,
		bool bHitCharacter,
		UWeaponDataAsset* FeedbackOverride = nullptr,
		float ExplosionRadiusOverride = -1.0f);

	TEAMPROJECT_FIN_API void ExecuteBounceCue(
		AWeaponBase* Weapon,
		AActor* InstigatorActor,
		UAbilitySystemComponent* SourceASC,
		const FHitResult& Hit,
		UWeaponDataAsset* FeedbackOverride = nullptr);
}
