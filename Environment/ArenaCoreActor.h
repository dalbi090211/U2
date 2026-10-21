#pragma once

#include "CoreMinimal.h"
#include "PCGMap/MapBuild/SectorBuild/Event/SectorEvent.h"
#include "ArenaCoreActor.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class ALight;

UENUM(BlueprintType)
enum class EArenaPhase : uint8
{
	Wait,
	Fight,
	Clear
};

USTRUCT(BlueprintType)
struct FArenaPhaseLook
{
	GENERATED_BODY()

	// 구체 발광과 조명에 같이 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor Color = FLinearColor::White;

	// 구체 머티리얼의 Intensity 파라미터 값
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float CoreIntensity = 5.f;

	// Light 컴포넌트에 설정한 Intensity 에 곱한다. 반경과 기준 밝기는 Light 에서 맞춘다
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float LightScale = 1.f;
};

// Execute 할 때마다 Wait -> Fight -> Clear 로 한 단계씩 넘어간다
UCLASS()
class TEAMPROJECT_FIN_API AArenaCoreActor : public AScenePlacedEvent
{
	GENERATED_BODY()

public:
	AArenaCoreActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Execute(const UObject* WorldContext = nullptr) override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Phase();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* Core;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UPointLightComponent* Light;

	// 단계 색을 같이 따라가는 레벨 배치 조명. 세기는 건드리지 않는다
	UPROPERTY(EditInstanceOnly, Category = "ArenaCore")
	TArray<TObjectPtr<ALight>> LinkedLights;

	UPROPERTY(EditAnywhere, Category = "ArenaCore")
	TMap<EArenaPhase, FArenaPhaseLook> PhaseLooks;

	UPROPERTY(EditAnywhere, Category = "ArenaCore", meta = (ClampMin = "0.0"))
	float TransitionSeconds = 2.f;

private:
	void BeginTransition();
	void ApplyLook(const FArenaPhaseLook& Look);
	const FArenaPhaseLook& LookOf(EArenaPhase InPhase) const;

	UPROPERTY(ReplicatedUsing = OnRep_Phase)
	EArenaPhase Phase = EArenaPhase::Wait;

	UPROPERTY()
	UMaterialInstanceDynamic* CoreMID = nullptr;

	// 전환 도중 단계가 또 바뀌면 지금 보이는 값에서 이어간다
	FArenaPhaseLook CurrentLook;
	FArenaPhaseLook FromLook;
	float BaseLightIntensity = 0.f;
	float TransitionElapsed = 0.f;
};
