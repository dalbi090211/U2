
#include "BaseCharacter.h"
#include "../Ability/AbilitySetDataAsset.h"
#include "GameCore/Weapon/Mods/SmartTracking/SmartTrackingSessionComponent.h"

#include "AbilitySystemComponent.h"
#include "../Attribute/CharacterAttributeSet.h"
#include "../Attribute/CharacterInitData.h"
#include "../Attribute/GE_InitAttributes.h"
#include "../Tags/GameplayTagSystem.h"
#include "../Weapon/Core/WeaponComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameCore/Animation/BaseCharacterAnimInstance.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "../../Environment/EnvironmentCharacterMovementComponent.h"

ABaseCharacter::ABaseCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	CharacterAttributeSet = CreateDefaultSubobject<UCharacterAttributeSet>(TEXT("CharacterAttributeSet"));

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_TraceAttack, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionProfileName(TEXT("CharacterHitbox"));
	}
	WeaponComponent = CreateDefaultSubobject<UWeaponComponent>(TEXT("WeaponComponent"));
	CreateDefaultSubobject<USmartTrackingSessionComponent>(TEXT("SmartTrackingSession"));

}

UAbilitySystemComponent* ABaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UCharacterAttributeSet* ABaseCharacter::GetCharacterAttributeSet() const
{
	return CharacterAttributeSet;
}

UWeaponComponent* ABaseCharacter::GetWeaponComponent() const
{
	return WeaponComponent;
}

void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	InitializeAbilitySystem();
}

void ABaseCharacter::InitializeAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		InitializeASC();
		BindAttributeDelegates();
		AbilitySystemComponent->RegisterGameplayTagEvent(GameplayTagSystem::State_Dead, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ABaseCharacter::OnDeadTagChanged);

		// 서버와 클라이언트 모두 자신의 역할 태그(피아식별용)를 부착해야 합니다.
		if (CharacterRoleTag.IsValid())
		{
			AbilitySystemComponent->AddLooseGameplayTag(CharacterRoleTag);
		}

		if (HasAuthority())
		{
			ApplyInitialStats();
			GrantCharacterRoleTag();
			GrantDefaultAbilities();
			ApplyDefaultEffects();
		}
	}

	BroadcastInitialAttributes();
}

void ABaseCharacter::InitializeASC()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

void ABaseCharacter::ApplyInitialStats()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// 시작 스탯 확정. 패시브(DefaultEffects)에 덮이지 않도록 가장 먼저 적용한다.
	// 행을 못 찾으면 데이터 테이블 경고만 남기고 기본값으로 굴러간다.
	if (const FCharacterInitStats* Stats = InitStatsRow.GetRow<FCharacterInitStats>(GetName()))
	{
		UGE_InitAttributes::ApplyTo(AbilitySystemComponent, *Stats);
	}
}

void ABaseCharacter::GrantCharacterRoleTag()
{
	if (AbilitySystemComponent && CharacterRoleTag.IsValid())
	{
		// 진영 태그(Character.Player, Character.Enemy) 영구 부여
		AbilitySystemComponent->AddLooseGameplayTag(CharacterRoleTag);
	}
}

void ABaseCharacter::GrantDefaultAbilities()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	TArray<TSubclassOf<UGameplayAbility>> Abilities = DefaultAbilities;
	if (SharedAbilitySet)
	{
		Abilities.Append(SharedAbilitySet->Abilities);
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : Abilities)
	{
		if (AbilityClass && !AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
		{
			FGameplayAbilitySpec AbilitySpec(AbilityClass, 1, INDEX_NONE, this);
			AbilitySystemComponent->GiveAbility(AbilitySpec);
		}
	}
}

void ABaseCharacter::ApplyDefaultEffects()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// 시작 시 기본 이펙트(패시브, 스태미나 자동 회복 등) 적용
	for (const TSubclassOf<UGameplayEffect>& EffectClass : DefaultEffects)
	{
		if (EffectClass)
		{
			FGameplayEffectContextHandle EffectContext = AbilitySystemComponent->MakeEffectContext();
			EffectContext.AddInstigator(this, this);

			FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(EffectClass, 1.0f, EffectContext);
			if (SpecHandle.IsValid())
			{
				AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}
}

void ABaseCharacter::BroadcastInitialAttributes()
{
	if (CharacterAttributeSet)
	{
		ApplyMovementSpeed(CharacterAttributeSet->GetMovementSpeed());

		// 클라는 어트리뷰트가 바인딩 전에 복제돼 올 수 있어 OnRep을 놓친다. 현재값으로 한 번 쏴 준다.
		OnHealthChanged.Broadcast(CharacterAttributeSet->GetHealth(), CharacterAttributeSet->GetMaxHealth());
		OnStaminaChanged.Broadcast(CharacterAttributeSet->GetStamina(), CharacterAttributeSet->GetMaxStamina());
		OnHealthCapMultiplierChanged.Broadcast(CharacterAttributeSet->GetHealthCapMultiplier());
		OnStaminaCapMultiplierChanged.Broadcast(CharacterAttributeSet->GetStaminaCapMultiplier());
	}
}

void ABaseCharacter::BindAttributeDelegates()
{
	if (!AbilitySystemComponent || !CharacterAttributeSet)
	{
		return;
	}

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(CharacterAttributeSet->GetMovementSpeedAttribute())
		.AddUObject(this, &ABaseCharacter::OnMovementSpeedChanged);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(CharacterAttributeSet->GetHealthAttribute())
		.AddUObject(this, &ABaseCharacter::OnHealthChangedCallback);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(CharacterAttributeSet->GetStaminaAttribute())
		.AddUObject(this, &ABaseCharacter::OnStaminaChangedCallback);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(CharacterAttributeSet->GetHealthCapMultiplierAttribute())
		.AddUObject(this, &ABaseCharacter::OnHealthCapMultiplierChangedCallback);

	AbilitySystemComponent
		->GetGameplayAttributeValueChangeDelegate(CharacterAttributeSet->GetStaminaCapMultiplierAttribute())
		.AddUObject(this, &ABaseCharacter::OnStaminaCapMultiplierChangedCallback);

	AbilitySystemComponent->RegisterGameplayTagEvent(
		FGameplayTag::RequestGameplayTag(FName("State.environment.snow.Level3")), 
		EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &ABaseCharacter::OnSnowDebuffTagChanged);
}

void ABaseCharacter::ApplyMovementSpeed(float NewMovementSpeed)
{
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->MaxWalkSpeed = FMath::Max(NewMovementSpeed, 0.0f);
		MovementComponent->MaxWalkSpeedCrouched = FMath::Max(NewMovementSpeed, 0.0f);
	}
}

void ABaseCharacter::OnMovementSpeedChanged(const FOnAttributeChangeData& Data)
{
	ApplyMovementSpeed(Data.NewValue);
}

void ABaseCharacter::OnHealthChangedCallback(const FOnAttributeChangeData& Data)
{
	if (CharacterAttributeSet)
	{
		OnHealthChanged.Broadcast(Data.NewValue, CharacterAttributeSet->GetMaxHealth());
	}
}

void ABaseCharacter::OnStaminaChangedCallback(const FOnAttributeChangeData& Data)
{
	if (CharacterAttributeSet)
	{
		OnStaminaChanged.Broadcast(Data.NewValue, CharacterAttributeSet->GetMaxStamina());
	}
}

void ABaseCharacter::OnHealthCapMultiplierChangedCallback(const FOnAttributeChangeData& Data)
{
	OnHealthCapMultiplierChanged.Broadcast(Data.NewValue);
}

void ABaseCharacter::OnStaminaCapMultiplierChangedCallback(const FOnAttributeChangeData& Data)
{
	OnStaminaCapMultiplierChanged.Broadcast(Data.NewValue);
}

void ABaseCharacter::OnSnowDebuffTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	OnSnowDebuffChanged.Broadcast(NewCount > 0);
}

void ABaseCharacter::BeginDeath()
{
	// 사망 GA가 직접 호출한 뒤 State.Dead 태그 콜백이 이어질 수 있다.
	// 이동/AI 정리 등의 사망 시작 처리는 반드시 한 번만 실행한다.
	if (bDeathStarted)
	{
		return;
	}

	bDeathStarted = true;
	OnDeathStarted.Broadcast(this);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();

		if (bDisableMovementOnDeathStart)
		{
			Movement->DisableMovement();
		}
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_TraceAttack, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetCollisionResponseToChannel(ECC_TraceAttack, ECR_Ignore);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
}

void ABaseCharacter::FinishDeath()
{
	if (bDeathFinished)
	{
		return;
	}

	bDeathFinished = true;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(
			ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
	}
}

void ABaseCharacter::Revive()
{
	bDeathStarted = false;
	bDeathFinished = false;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}

	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetCollisionResponseToChannel(ECC_TraceAttack, ECR_Block);
		MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // 메시 기본 프리셋은 보통 Pawn을 무시한다.
		MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

		// 사망 몽타주가 저장한 자세가 남으면 풀에서 다시 꺼낸 몹이 쓰러진 모습으로 싸운다
		if (UBaseCharacterAnimInstance* AnimInstance = Cast<UBaseCharacterAnimInstance>(MeshComponent->GetAnimInstance()))
		{
			AnimInstance->ClearDeathPose();
		}
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
}

void ABaseCharacter::OnDeadTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	if (NewCount > 0)
	{
		BeginDeath();
	}
	else
	{
		Revive();
	}
}
