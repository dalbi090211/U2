#include "GE_InitAttributes.h"

#include "AbilitySystemComponent.h"
#include "CharacterAttributeSet.h"
#include "CharacterInitData.h"

namespace
{
	// SetByCaller 키. 모디파이어 정의와 주입 코드가 같은 이름을 쓰도록 한곳에 모아둔다.
	const FName Key_MaxHealth(TEXT("Init.MaxHealth"));
	const FName Key_MaxStamina(TEXT("Init.MaxStamina"));
	const FName Key_StaminaRegenRate(TEXT("Init.StaminaRegenRate"));
	const FName Key_MovementSpeed(TEXT("Init.MovementSpeed"));
	const FName Key_Armor(TEXT("Init.Armor"));
	const FName Key_AttackPower(TEXT("Init.AttackPower"));

	FGameplayModifierInfo MakeOverride(const FGameplayAttribute& Attribute, const FName& Key)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataName = Key;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Override;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);

		return Modifier;
	}
}

UGE_InitAttributes::UGE_InitAttributes()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	// Max를 먼저 올린 뒤 현재값을 채운다. 순서가 바뀌면 이전 Max에 클램프되어 잘린다.
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetMaxHealthAttribute(), Key_MaxHealth));
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetHealthAttribute(), Key_MaxHealth));
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetMaxStaminaAttribute(), Key_MaxStamina));
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetStaminaAttribute(), Key_MaxStamina));
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetStaminaRegenRateAttribute(), Key_StaminaRegenRate));
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetMovementSpeedAttribute(), Key_MovementSpeed));
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetArmorAttribute(), Key_Armor));
	Modifiers.Add(MakeOverride(UCharacterAttributeSet::GetAttackPowerAttribute(), Key_AttackPower));
}

void UGE_InitAttributes::ApplyTo(UAbilitySystemComponent* ASC, const FCharacterInitStats& Stats)
{
	if (!ASC)
	{
		return;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(ASC->GetOwnerActor(), ASC->GetAvatarActor());

	FGameplayEffectSpecHandle SpecHandle = ASC->MakeOutgoingSpec(StaticClass(), 1.0f, Context);
	if (!SpecHandle.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[스탯 초기화 실패] 스펙 생성 불가"));
		return;
	}

	FGameplayEffectSpec& Spec = *SpecHandle.Data.Get();

	// 모디파이어가 참조하는 키를 하나라도 빠뜨리면 그 어트리뷰트가 0으로 덮인다.
	Spec.SetSetByCallerMagnitude(Key_MaxHealth, Stats.MaxHealth);
	Spec.SetSetByCallerMagnitude(Key_MaxStamina, Stats.MaxStamina);
	Spec.SetSetByCallerMagnitude(Key_StaminaRegenRate, Stats.StaminaRegenRate);
	Spec.SetSetByCallerMagnitude(Key_MovementSpeed, Stats.MovementSpeed);
	Spec.SetSetByCallerMagnitude(Key_Armor, Stats.Armor);
	Spec.SetSetByCallerMagnitude(Key_AttackPower, Stats.AttackPower);

	ASC->ApplyGameplayEffectSpecToSelf(Spec);

	if (const UCharacterAttributeSet* Attributes = ASC->GetSet<UCharacterAttributeSet>())
	{
		UE_LOG(LogTemp, Warning, TEXT("[스탯 초기화] %s | 체력 %.0f/%.0f | 속도 %.0f | 공격력 x%.2f"),
			*GetNameSafe(ASC->GetAvatarActor()),
			Attributes->GetHealth(), Attributes->GetMaxHealth(),
			Attributes->GetMovementSpeed(), Attributes->GetAttackPower());
	}
}
