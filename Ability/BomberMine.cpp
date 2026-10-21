#include "BomberMine.h"

#include "../Character/BaseCharacter.h"
#include "../Projectile/MonsterProjectile.h"	//UNiagaraSoundCueData
#include "../Tags/GameplayTagSystem.h"
#include "../Weapon/Libraries/WeaponDamageLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

ABomberMine::ABomberMine()
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = false;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(16.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComponent->SetCollisionObjectType(ECC_TraceAttack);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	SetRootComponent(CollisionComponent);

	MineMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MineMesh"));
	MineMeshComponent->SetupAttachment(CollisionComponent);
	MineMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComponent;
	ProjectileMovement->bRotationFollowsVelocity = true;

	//튕기다 마찰로 멈춘다. 멈춘 뒤에는 컴포넌트가 시뮬레이션을 꺼서 굴러다니지 않는다
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.2f;
	ProjectileMovement->Friction = 0.6f;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
	ProjectileMovement->OnProjectileBounce.AddDynamic(this, &ABomberMine::OnProjectileBounce);
}

void ABomberMine::Throw(
	AActor* InInstigatorActor,
	UAbilitySystemComponent* InSourceASC,
	const FVector& LaunchVelocity,
	float InAbilityLevel,
	float InArmedSeconds)
{
	InstigatorActor = InInstigatorActor;
	SourceASC = InSourceASC;
	AbilityLevel = InAbilityLevel;
	SetInstigator(Cast<APawn>(InInstigatorActor));

	//던진 본인 캡슐에 걸려 발밑에 떨어지는 것을 막는다
	if (InInstigatorActor)
	{
		CollisionComponent->IgnoreActorWhenMoving(InInstigatorActor, true);
	}

	ProjectileMovement->Velocity = LaunchVelocity;
	ProjectileMovement->Activate(true);
	MulticastPlaySound(ThrowSound, GetActorLocation());

	//신관. 플레이어가 끝내 누르지 않으면 여기서 터진다
	if (InArmedSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			FuseTimerHandle,
			this,
			&ABomberMine::Detonate,
			InArmedSeconds,
			false);
	}
}

void ABomberMine::Detonate()
{
	if (!HasAuthority() || bDetonated)
	{
		return;
	}
	bDetonated = true;

	const FVector Origin = GetActorLocation();
	MulticastPlaySound(ExplosionSound, Origin);

	if (DamageEffectClass && ExplosionRadius > 0.0f && ExplosionDamage > 0.0f)
	{
		FWeaponRadialDamageRequest DamageRequest;
		DamageRequest.World = GetWorld();
		DamageRequest.Origin = Origin;
		DamageRequest.Radius = ExplosionRadius;
		DamageRequest.InstigatorActor = InstigatorActor;
		DamageRequest.SourceASC = SourceASC;
		DamageRequest.DamageEffectClass = DamageEffectClass;
		DamageRequest.DamageAmount = ExplosionDamage;
		DamageRequest.AbilityLevel = AbilityLevel;
		DamageRequest.bHostileTargetsOnly = true;
		WeaponDamageLibrary::ApplyGasRadialDamage(DamageRequest);
	}

	ApplyKnockback(Origin);

	if (SourceASC)
	{
		FGameplayCueParameters CueParameters;
		CueParameters.Instigator = InstigatorActor;
		CueParameters.EffectCauser = this;
		CueParameters.Location = Origin;
		CueParameters.RawMagnitude = FMath::Max(ExplosionRadius, 0.0f);
		CueParameters.SourceObject = ExplosionCueData;
		SourceASC->ExecuteGameplayCue(GameplayTagSystem::GameplayCue_Skill_Bomber_MineExplode, CueParameters);

		//지속시간 GE를 걷는 곳은 여기 한 군데. 눌러서 터지든 신관으로 터지든 같은 경로를 탄다
		SourceASC->RemoveActiveEffectsWithGrantedTags(
			FGameplayTagContainer(GameplayTagSystem::State_Skill_Bomber_MineArmed));
	}

	Destroy();
}

void ABomberMine::OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	if (!HasAuthority() || bDetonated || bLanded || ImpactResult.ImpactNormal.Z <= 0.0f)
	{
		return;
	}
	bLanded = true;
	MulticastPlaySound(LandingSound, ImpactResult.ImpactPoint);
}

void ABomberMine::MulticastPlaySound_Implementation(USoundBase* Sound, FVector_NetQuantize Location)
{
	if (GetNetMode() != NM_DedicatedServer && IsValid(Sound))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location);
	}
}

void ABomberMine::ApplyKnockback(const FVector& Origin)
{
	//InstigatorActor는 서버에만 있다. Instigator는 복제되므로 클라이언트도 같은 폰을 본다
	ACharacter* InstigatorCharacter = Cast<ACharacter>(GetInstigator());
	if (!InstigatorCharacter || KnockbackStrength <= 0.0f || KnockbackRadius <= 0.0f)
	{
		return;
	}

	const FVector ToInstigator = InstigatorCharacter->GetActorLocation() - Origin;
	const float Distance = ToInstigator.Size();
	if (Distance > KnockbackRadius)
	{
		return;
	}

	//발밑에서 1, 반경 끝에서 KnockbackEdgeScale까지 선형으로 준다
	const float Falloff = FMath::Lerp(1.0f, KnockbackEdgeScale, Distance / KnockbackRadius);

	//수평과 수직을 따로 준다. 발밑에서 터져 수평 방향이 없으면 위로만 뜬다
	const FVector HorizontalDirection = FVector(ToInstigator.X, ToInstigator.Y, 0.0f).GetSafeNormal();
	const FVector LaunchVelocity =
		(HorizontalDirection * KnockbackStrength + FVector::UpVector * KnockbackUpSpeed) * Falloff;

	//서버가 권위. 리슨 서버 호스트면 아래 RPC가 같은 값으로 덮어써도 결과는 같다
	InstigatorCharacter->LaunchCharacter(LaunchVelocity, true, true);
	ClientLaunchInstigator(LaunchVelocity);
}

void ABomberMine::ClientLaunchInstigator_Implementation(FVector LaunchVelocity)
{
	if (ACharacter* InstigatorCharacter = Cast<ACharacter>(GetInstigator()))
	{
		InstigatorCharacter->LaunchCharacter(LaunchVelocity, true, true);
	}
}
