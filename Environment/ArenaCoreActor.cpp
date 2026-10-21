#include "ArenaCoreActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/LightComponent.h"
#include "Engine/Light.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FName CoreColorParam(TEXT("CoreColor"));
	const FName CoreIntensityParam(TEXT("Intensity"));

	FArenaPhaseLook MakeLook(const FLinearColor& Color, float CoreIntensity, float LightScale)
	{
		FArenaPhaseLook Look;
		Look.Color = Color;
		Look.CoreIntensity = CoreIntensity;
		Look.LightScale = LightScale;
		return Look;
	}

	FArenaPhaseLook LerpLook(const FArenaPhaseLook& A, const FArenaPhaseLook& B, float Alpha)
	{
		return MakeLook(FMath::Lerp(A.Color, B.Color, Alpha),
			FMath::Lerp(A.CoreIntensity, B.CoreIntensity, Alpha),
			FMath::Lerp(A.LightScale, B.LightScale, Alpha));
	}
}

AArenaCoreActor::AArenaCoreActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	// 조명이 경기장 전체를 비추므로 거리와 상관없이 단계가 전달돼야 한다
	bAlwaysRelevant = true;

	Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	RootComponent = Core;
	// 조명이 구체 안에 있어서, 구체가 그림자를 드리우면 빛이 밖으로 못 나간다
	Core->SetCastShadow(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Core);

	PhaseLooks.Add(EArenaPhase::Wait, MakeLook(FLinearColor(0.8f, 0.85f, 1.f), 5.f, 1.f));
	PhaseLooks.Add(EArenaPhase::Fight, MakeLook(FLinearColor(1.f, 0.12f, 0.06f), 12.f, 1.8f));
	PhaseLooks.Add(EArenaPhase::Clear, MakeLook(FLinearColor(1.f, 0.75f, 0.35f), 8.f, 1.3f));
}

void AArenaCoreActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaCoreActor, Phase);
}

void AArenaCoreActor::BeginPlay()
{
	Super::BeginPlay();

	CoreMID = Core->CreateAndSetMaterialInstanceDynamic(0);
	checkf(CoreMID, TEXT("[ArenaCore] 구체 머티리얼 없음: %s"), *GetName());
	FLinearColor ColorProbe;
	float ScalarProbe;
	verifyf(CoreMID->GetVectorParameterValue(FHashedMaterialParameterInfo(CoreColorParam), ColorProbe),
		TEXT("[ArenaCore] 머티리얼에 %s 벡터 파라미터 없음: %s"), *CoreColorParam.ToString(), *GetName());
	verifyf(CoreMID->GetScalarParameterValue(FHashedMaterialParameterInfo(CoreIntensityParam), ScalarProbe),
		TEXT("[ArenaCore] 머티리얼에 %s 스칼라 파라미터 없음: %s"), *CoreIntensityParam.ToString(), *GetName());

	for (const ALight* Linked : LinkedLights)
	{
		checkf(Linked, TEXT("[ArenaCore] LinkedLights 에 빈 칸: %s"), *GetName());
		// Static 조명은 런타임 색 변경이 엔진에서 조용히 무시된다
		checkf(Linked->GetLightComponent()->Mobility != EComponentMobility::Static,
			TEXT("[ArenaCore] Static 조명은 색을 바꿀 수 없음: %s (%s)"), *Linked->GetName(), *GetName());
	}

	BaseLightIntensity = Light->Intensity;
	CurrentLook = LookOf(Phase);
	ApplyLook(CurrentLook);
}

void AArenaCoreActor::Execute(const UObject* WorldContext)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!ensureMsgf(Phase != EArenaPhase::Clear, TEXT("[ArenaCore] Clear 이후 다음 단계 없음: %s"), *GetName()))
	{
		return;
	}

	Super::Execute(WorldContext);
	Phase = static_cast<EArenaPhase>(static_cast<uint8>(Phase) + 1);
	// 서버에서는 OnRep 이 불리지 않는다
	BeginTransition();
	RunEventGroups(PostEvents);
}

void AArenaCoreActor::OnRep_Phase()
{
	// 초기 복제는 BeginPlay 보다 먼저 올 수 있다. 그때는 BeginPlay 가 현재 단계로 바로 맞춘다
	if (!HasActorBegunPlay())
	{
		return;
	}
	BeginTransition();
}

void AArenaCoreActor::BeginTransition()
{
	FromLook = CurrentLook;
	TransitionElapsed = 0.f;
	SetActorTickEnabled(true);
}

void AArenaCoreActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TransitionElapsed += DeltaSeconds;
	const float Alpha = TransitionSeconds > 0.f ? FMath::Min(TransitionElapsed / TransitionSeconds, 1.f) : 1.f;
	CurrentLook = LerpLook(FromLook, LookOf(Phase), FMath::SmoothStep(0.f, 1.f, Alpha));
	ApplyLook(CurrentLook);

	if (Alpha >= 1.f)
	{
		SetActorTickEnabled(false);
	}
}

void AArenaCoreActor::ApplyLook(const FArenaPhaseLook& Look)
{
	CoreMID->SetVectorParameterValue(CoreColorParam, Look.Color);
	CoreMID->SetScalarParameterValue(CoreIntensityParam, Look.CoreIntensity);
	Light->SetLightColor(Look.Color);
	Light->SetIntensity(BaseLightIntensity * Look.LightScale);
	for (ALight* Linked : LinkedLights)
	{
		Linked->GetLightComponent()->SetLightColor(Look.Color);
	}
}

const FArenaPhaseLook& AArenaCoreActor::LookOf(EArenaPhase InPhase) const
{
	const FArenaPhaseLook* Look = PhaseLooks.Find(InPhase);
	checkf(Look, TEXT("[ArenaCore] 단계 연출값 없음: Phase=%d, %s"), (int32)InPhase, *GetName());
	return *Look;
}
