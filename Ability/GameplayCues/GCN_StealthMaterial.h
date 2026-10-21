#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "Engine/Scene.h"
#include "GCN_StealthMaterial.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UMeshComponent;

USTRUCT()
struct FStealthMeshMaterials
{
	GENERATED_BODY()

	TWeakObjectPtr<UMeshComponent> Mesh;

	//UPROPERTY가 없으면 은신 도중 원본이 GC로 날아가 복구가 깨진다
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;
};

/*
 * 은신 연출. GE가 붙고 떨어질 때 자동으로 OnActive / OnRemove가 돈다.
 *
 *   머티리얼   모든 머신에서 교체된다. 다른 플레이어에게도 보인다
 *   포스트프로세스  로컬 플레이어에게만 적용된다. 남의 화면 색을 바꾸면 안 되니까
 */
UCLASS(Blueprintable)
class TEAMPROJECT_FIN_API AGCN_StealthMaterial : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()

public:
	AGCN_StealthMaterial();

	virtual void Tick(float DeltaSeconds) override;

	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool WhileActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
	virtual bool Recycle() override;

protected:
	//모든 머티리얼 슬롯을 이것으로 덮는다. 반투명 은신 머티리얼
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|Material")
	TObjectPtr<UMaterialInterface> StealthMaterial;

	//로컬 플레이어 카메라에만 얹는다. 푸른 톤이면 Color Gain / Scene Color Tint를 만진다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|PostProcess")
	FPostProcessSettings StealthPostProcess;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|PostProcess", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PostProcessBlendWeight = 1.0f;

	//0이면 즉시 전환
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|PostProcess", meta = (ClampMin = "0.0"))
	float BlendInTime = 0.35f;

	//해제는 들킨 순간이라 짧게. 완전히 0이면 뚝 끊긴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue|PostProcess", meta = (ClampMin = "0.0"))
	float BlendOutTime = 0.12f;

private:
	void ApplyStealth(AActor* MyTarget);
	void RestoreStealth();

	//블렌드 아웃이 끝난 뒤 원복하고 Cue 종료를 알린다
	void FinishBlendOut();

	static UCameraComponent* FindLocalCamera(AActor* MyTarget);

	//캐릭터 메시와 거기에 붙은 메시 전부. 무기 액터도 메시 소켓에 붙으므로 여기 걸린다
	static void CollectStealthMeshes(AActor* MyTarget, TArray<UMeshComponent*>& OutMeshes);

	bool bApplied = false;
	bool bBlendingOut = false;
	float BlendElapsed = 0.0f;

	//블렌드를 시작한 시점의 가중치. 페이드 인 도중에 나가도 튀지 않게 한다
	float BlendStartWeight = 0.0f;

	UPROPERTY()
	TArray<FStealthMeshMaterials> AppliedMeshes;

	TWeakObjectPtr<UCameraComponent> AppliedCamera;
	FPostProcessSettings OriginalPostProcess;
	float OriginalBlendWeight = 0.0f;
};
