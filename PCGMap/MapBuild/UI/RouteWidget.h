#pragma once

#include "CoreMinimal.h"
#include "RouteNode.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "TeamProject_Fin/PCGMap/MapBuild/MapStruct.h"
#include "RouteWidget.generated.h"

UCLASS()
class TEAMPROJECT_FIN_API URouteWidget : public UUserWidget
{
	GENERATED_BODY()
    
public:
	// LayoutSeed 는 런 시드. 지터를 전역 randStream 에서 뽑으면 머신마다 노드 위치가 어긋난다.
	void Init(FVirRoute* OutVirtualRoute, int32 LayoutSeed);
	void VisibleUI(FIntPoint pos);

	// 노드를 고른 뒤 닫는다. CloseAnim 이 끝나야 서버로 선택이 넘어간다
	void CloseUI(FIntPoint SelectedPos);

	void ActiveWidget(FIntPoint pos);

	// 지나온 노드에 체크를 켠다. 좌표는 curPos 와 같은 (X, Y) 형식
	void ApplyVisited(const TArray<FIntPoint>& Visited);

	void SetPosition(int32 Y);

	/*
	 * WBP 의 Animations 패널에서 같은 이름으로 만들면 자동으로 붙는다. 없어도 동작한다.
	 * 위젯은 재사용되므로 CloseAnim 이 건드린 값은 OpenAnim 이 되돌려 놓아야 한다
	 */
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> OpenAnim;

	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> CloseAnim;
    
	UPROPERTY(EditDefaultsOnly, Category = "Route Settings|Classes")
	TSubclassOf<URouteNode> RouteNodeClass;
    
	UPROPERTY(EditDefaultsOnly, Category = "Route Settings|Classes")
	TSubclassOf<UUserWidget> EdgeDotClass;
    
	UPROPERTY(meta = (BindWidget))
	UCanvasPanel* NodeContainer;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Route Settings|Layout")
	float PosYOffset = 150.f;
    
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Route Settings|Layout")
	float levelDistance = 150.f;
    
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Route Settings|Layout")
	float nodeSpacing = 150.f;
    
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Route Settings|Noise")
	float nodeNoiseRange = 20.f; // 노드 위치에 주는 지터 (Jitter)
    
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Route Settings|Noise")
	float edgeNoiseRange = 15.f; // 간선 점들에 주는 지터
    
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Route Settings|Edge")
	float dotSpacing = 20.f; // 점 사이 간격
    
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FVector2D NodeSize = FVector2D(64.f, 64.f);
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Route Settings|Edge")
	FVector2D DotSize = FVector2D(8.f, 8.f);

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    
private:
	UFUNCTION()
	void OnCloseAnimFinished();

	void generateWidgets();
	void generateEdges();
	void SetupContainerSize();
	FVector2D GetNodePosition(int32 Y, int32 X) const;
	void SpawnEdgeDots(const FVector2D& Start, const FVector2D& End, FIntPoint StartCoord, FIntPoint EndCoord);
	void activeConnectedWidgets(FIntPoint pos);
	
	FVirRoute* routePointer;
	FRandomStream LayoutRand;
	TArray<TArray<FVector2D>> NodePositions;
	
	TMap<FIntPoint, URouteNode*> NodeMap;
	TMap<TPair<FIntPoint,FIntPoint>, TArray<UUserWidget*>> EdgeMap;
    
	float TotalHeight;
	float TotalWidth;

	bool bIsDragging = false;

	// 닫는 중에는 두 번째 선택을 받지 않는다
	bool bClosing = false;
	FIntPoint PendingSelect = FIntPoint::ZeroValue;
};