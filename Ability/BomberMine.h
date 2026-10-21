#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BomberMine.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;
class UNiagaraSoundCueData;
class UProjectileMovementComponent;
class USphereComponent;
class USoundBase;

//폭파병이 던지는 지뢰. 던지면 굴러가 멈추고, 두 번째 입력이나 신관 만료로 Detonate가 불린다.
//스폰과 폭발은 서버 몫이고 클라이언트는 액터 복제로 위치만 본다
UCLASS()
class TEAMPROJECT_FIN_API ABomberMine : public AActor
{
	GENERATED_BODY()

public:
	ABomberMine();

	//던진 직후 한 번. 스폰한 쪽(서버)이 호출한다.
	//InArmedSeconds는 지속시간 GE와 같은 값이어야 한다. 출처는 스킬 하나뿐이다
	void Throw(
		AActor* InInstigatorActor,
		UAbilitySystemComponent* InSourceASC,
		const FVector& LaunchVelocity,
		float InAbilityLevel,
		float InArmedSeconds);

	//반경 데미지를 넣고 지속시간 GE를 걷어낸 뒤 스스로 파괴된다. 두 번 불러도 한 번만 터진다
	void Detonate();

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Sound")
	TObjectPtr<USoundBase> ThrowSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Sound")
	TObjectPtr<USoundBase> LandingSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Sound")
	TObjectPtr<USoundBase> ExplosionSound;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<UStaticMeshComponent> MineMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Explosion", meta = (ClampMin = "0.0"))
	float ExplosionRadius = 400.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Explosion", meta = (ClampMin = "0.0"))
	float ExplosionDamage = 150.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Explosion")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/*
	 * 폭발 이펙트와 사운드. GameplayCue.Skill.Bomber.MineExplode 큐에
	 * SourceObject로 실려 나간다. 큐는 UGCN_SpawnNiagaraAndSoundScaled 파생으로 만든다.
	 * 비워두면 큐 에셋의 기본값으로 나간다
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Explosion")
	TObjectPtr<UNiagaraSoundCueData> ExplosionCueData;

	//반동은 지뢰를 던진 본인에게만 들어간다. 다른 캐릭터는 데미지만 받는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Knockback", meta = (ClampMin = "0.0"))
	float KnockbackRadius = 450.0f;

	//폭심에서 폭발 반대 방향으로 밀리는 수평 속도. 0이면 반동 없음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Knockback", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockbackStrength = 1300.0f;

	//반경 끝에서 받는 힘의 비율. 1이면 거리와 무관하게 같은 힘, 0이면 끝에서 아무것도 안 받는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Knockback", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KnockbackEdgeScale = 0.7f;

	//수평과 별개로 더해지는 위쪽 속도. 0으로 두면 지면 마찰에 막혀 거의 밀리지 않는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mine|Knockback", meta = (ClampMin = "0.0", Units = "cm/s"))
	float KnockbackUpSpeed = 350.0f;

private:
	UFUNCTION()
	void OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlaySound(USoundBase* Sound, FVector_NetQuantize Location);

	//시전자를 폭심 반대 방향으로 띄운다. 발밑에서 터뜨리면 슈퍼점프가 된다
	void ApplyKnockback(const FVector& Origin);

	//지뢰의 Owner가 시전자라 이 RPC는 시전자 클라이언트로 간다.
	//서버 보정만 기다리면 점프가 한 박자 늦게 뜬다
	UFUNCTION(Client, Reliable)
	void ClientLaunchInstigator(FVector LaunchVelocity);

	UPROPERTY()
	TObjectPtr<AActor> InstigatorActor;

	UPROPERTY()
	TObjectPtr<UAbilitySystemComponent> SourceASC;

	FTimerHandle FuseTimerHandle;

	float AbilityLevel = 1.0f;
	bool bDetonated = false;
	bool bLanded = false;
};
