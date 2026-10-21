#include "GCN_StealthMaterial.h"

#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"

AGCN_StealthMaterial::AGCN_StealthMaterial()
{
	//블렌드가 끝나면 스스로 끈다
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bAutoDestroyOnRemove = false;
}

void AGCN_StealthMaterial::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UCameraComponent* Camera = AppliedCamera.Get();
	if (!Camera)
	{
		SetActorTickEnabled(false);
		if (bBlendingOut)
		{
			FinishBlendOut();
		}
		return;
	}

	BlendElapsed += DeltaSeconds;

	const float Duration = bBlendingOut ? BlendOutTime : BlendInTime;
	const float Linear = Duration > 0.0f
		? FMath::Clamp(BlendElapsed / Duration, 0.0f, 1.0f)
		: 1.0f;

	//smoothstep. 선형으로 올리면 시작과 끝이 딱딱하다
	const float Eased = Linear * Linear * (3.0f - 2.0f * Linear);
	const float TargetWeight = bBlendingOut ? 0.0f : PostProcessBlendWeight;
	Camera->PostProcessBlendWeight = FMath::Lerp(BlendStartWeight, TargetWeight, Eased);

	if (Linear >= 1.0f)
	{
		SetActorTickEnabled(false);
		if (bBlendingOut)
		{
			FinishBlendOut();
		}
	}
}

bool AGCN_StealthMaterial::OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	ApplyStealth(MyTarget);
	return true;
}

bool AGCN_StealthMaterial::WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	//늦게 접속했거나 관련성이 뒤늦게 생긴 클라이언트는 OnActive를 놓치고 여기로 들어온다
	ApplyStealth(MyTarget);
	return true;
}

bool AGCN_StealthMaterial::OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{

	UCameraComponent* Camera = AppliedCamera.Get();
	if (!bApplied || BlendOutTime <= 0.0f || !Camera)
	{
		FinishBlendOut();
		return true;
	}

	//지금 가중치에서 0으로 내린다. 페이드 인 도중에 쐈으면 그 중간값에서 이어진다
	bBlendingOut = true;
	BlendElapsed = 0.0f;
	BlendStartWeight = Camera->PostProcessBlendWeight;
	SetActorTickEnabled(true);

	//원복과 GameplayCueFinishedCallback은 블렌드가 끝난 뒤에. 그 전에 부르면 액터가 회수된다
	return true;
}

void AGCN_StealthMaterial::FinishBlendOut()
{
	bBlendingOut = false;
	RestoreStealth();
	GameplayCueFinishedCallback();
}

bool AGCN_StealthMaterial::Recycle()
{
	//인스턴스가 재사용되므로 캐시를 반드시 비운다. 안 그러면 남의 원본 머티리얼로 복구된다.
	//블렌드 아웃 중에 회수되면 여기서 즉시 마무리된다
	bBlendingOut = false;
	RestoreStealth();
	return Super::Recycle();
}

void AGCN_StealthMaterial::ApplyStealth(AActor* MyTarget)
{
	//나가는 중에 다시 들어왔다. 원복하지 않고 현재 가중치에서 방향만 뒤집는다
	if (bBlendingOut)
	{
		UCameraComponent* BlendingCamera = AppliedCamera.Get();
		bBlendingOut = false;
		BlendElapsed = 0.0f;
		BlendStartWeight = BlendingCamera ? BlendingCamera->PostProcessBlendWeight : 0.0f;
		SetActorTickEnabled(true);
		return;
	}

	if (bApplied || !MyTarget)
	{
		return;
	}
	bApplied = true;

	// 머티리얼: 모든 머신에서 교체한다
	//Used with Skeletal Mesh가 꺼져 있으면 렌더러가 기본 머티리얼로 대체한다. 교체해도 벗겨진 것처럼 보인다.
	//에디터에서는 이 호출이 플래그를 켜고 재컴파일한다. 머티리얼 에셋을 저장해야 유지된다
	if (StealthMaterial && StealthMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh))
	{
		TArray<UMeshComponent*> Meshes;
		CollectStealthMeshes(MyTarget, Meshes);

		for (UMeshComponent* TargetMesh : Meshes)
		{
			FStealthMeshMaterials& Entry = AppliedMeshes.AddDefaulted_GetRef();
			Entry.Mesh = TargetMesh;
			Entry.Materials = TargetMesh->GetMaterials();

			for (int32 SlotIndex = 0; SlotIndex < Entry.Materials.Num(); ++SlotIndex)
			{
				TargetMesh->SetMaterial(SlotIndex, StealthMaterial);
			}
		}
	}

	// 포스트프로세스: 자기 화면에만
	UCameraComponent* Camera = FindLocalCamera(MyTarget);
	if (!Camera)
	{
		return;
	}

	OriginalPostProcess = Camera->PostProcessSettings;
	OriginalBlendWeight = Camera->PostProcessBlendWeight;

	//0에서 시작해 Tick이 올린다. 목표 가중치로 바로 넣으면 한 프레임 만에 튄다
	Camera->PostProcessSettings = StealthPostProcess;
	Camera->PostProcessBlendWeight = 0.0f;
	AppliedCamera = Camera;

	BlendElapsed = 0.0f;
	BlendStartWeight = 0.0f;
	SetActorTickEnabled(true);
}

void AGCN_StealthMaterial::RestoreStealth()
{
	if (!bApplied)
	{
		return;
	}
	bApplied = false;

	SetActorTickEnabled(false);
	BlendElapsed = 0.0f;
	BlendStartWeight = 0.0f;

	for (const FStealthMeshMaterials& Entry : AppliedMeshes)
	{
		//은신 중에 무기를 버렸거나 캐릭터가 죽었으면 컴포넌트가 없다
		UMeshComponent* Mesh = Entry.Mesh.Get();
		if (!Mesh)
		{
			continue;
		}

		for (int32 SlotIndex = 0; SlotIndex < Entry.Materials.Num(); ++SlotIndex)
		{
			Mesh->SetMaterial(SlotIndex, Entry.Materials[SlotIndex]);
		}
	}
	AppliedMeshes.Reset();

	if (UCameraComponent* Camera = AppliedCamera.Get())
	{
		Camera->PostProcessSettings = OriginalPostProcess;
		Camera->PostProcessBlendWeight = OriginalBlendWeight;
	}
	AppliedCamera.Reset();
	OriginalPostProcess = FPostProcessSettings();
	OriginalBlendWeight = 0.0f;
}

UCameraComponent* AGCN_StealthMaterial::FindLocalCamera(AActor* MyTarget)
{
	const APawn* TargetPawn = Cast<APawn>(MyTarget);
	if (!TargetPawn)
	{
		return nullptr;
	}

	if (!TargetPawn->IsLocallyControlled())
	{
		//남의 캐릭터에 붙은 Cue다. 화면 색은 건드리지 않는다
		return nullptr;
	}

	UCameraComponent* Camera = MyTarget->FindComponentByClass<UCameraComponent>();
	return Camera;
}

void AGCN_StealthMaterial::CollectStealthMeshes(AActor* MyTarget, TArray<UMeshComponent*>& OutMeshes)
{
	const ACharacter* TargetCharacter = Cast<ACharacter>(MyTarget);
	USkeletalMeshComponent* Mesh = TargetCharacter ? TargetCharacter->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}

	OutMeshes.Add(Mesh);

	//무기는 별개 액터지만 WeaponComponent::EquipWeapon이 캐릭터 메시 소켓에 붙인다.
	//자식 컴포넌트를 훑으면 총과 총열이 같이 딸려온다
	TArray<USceneComponent*> Children;
	Mesh->GetChildrenComponents(true, Children);
	for (USceneComponent* Child : Children)
	{
		if (UMeshComponent* ChildMesh = Cast<UMeshComponent>(Child))
		{
			OutMeshes.Add(ChildMesh);
		}
	}
}
