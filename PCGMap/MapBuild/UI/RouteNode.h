// RouteNode.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TeamProject_Fin/PCGMap/MapBuild/MapStruct.h"
#include "RouteNode.generated.h"

UENUM(BlueprintType)
enum class ERouteNodeState : uint8
{
	Locked     UMETA(DisplayName = "선택 불가"), // 탁한 색
	Selectable UMETA(DisplayName = "선택 가능"), // 원래 색
	Visited    UMETA(DisplayName = "지나온 노드") // 원래 색 + 체크
};

UCLASS()
class TEAMPROJECT_FIN_API URouteNode : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void Init(ESectorType Type, FIntPoint Pos);

	// 노드 클릭 시 BP 에서 호출. 좌표도 타입도 넘길 필요 없다 — 서버가 다 알고 있다.
	UFUNCTION(BlueprintCallable)
	void Select();

	// 상태가 바뀔 때마다 OnTextureSet 이 다시 불린다. 겉모습은 전부 거기서 정한다
	UFUNCTION(BlueprintCallable)
	void SetState(ERouteNodeState NewState);

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	ESectorType SectorType;

	UPROPERTY(BlueprintReadOnly)
	ERouteNodeState NodeState = ERouteNodeState::Locked;

	UPROPERTY(BlueprintReadOnly, EditAnywhere)
	FIntPoint nodePos;

protected:
	// 타입은 아이콘, 상태는 색과 체크 표시를 정한다
	UFUNCTION(BlueprintImplementableEvent)
	void OnTextureSet(ESectorType Type, ERouteNodeState State);

private :
};