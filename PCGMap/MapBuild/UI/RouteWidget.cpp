#include "RouteWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Components/CanvasPanelSlot.h"
#include "GameCore/BasePlayerController.h"
#include "Math/UnrealMathUtility.h"

void URouteWidget::Init(FVirRoute* OutVirtualRoute, int32 LayoutSeed)
{
    routePointer = OutVirtualRoute;
    LayoutRand.Initialize(LayoutSeed);

    if (routePointer)
    {
       TotalHeight = (routePointer->MaxY - 1) * levelDistance;
       TotalWidth = (routePointer->MaxX - 1) * nodeSpacing;
       
        SetupContainerSize();
        generateWidgets();
        generateEdges();
        SetPosition(0);
    }
}

void URouteWidget::SetupContainerSize()
{
   UCanvasPanelSlot* ContainerSlot = Cast<UCanvasPanelSlot>(NodeContainer->Slot);
   ContainerSlot->SetSize(FVector2D(TotalWidth, TotalHeight));
}

void URouteWidget::SetPosition(int32 Y)
{
   UCanvasPanelSlot* ContainerSlot = Cast<UCanvasPanelSlot>(NodeContainer->Slot);
   if (!ContainerSlot || !routePointer)
   {
      return;
   }

   float MaxScroll = FMath::Max(0.f, TotalHeight);
   float testY = FMath::Clamp(-(routePointer->MaxY - (float)Y) * levelDistance - PosYOffset, -MaxScroll, 0.f);

   FVector2D CurrentPos = ContainerSlot->GetPosition();
   ContainerSlot->SetPosition(FVector2D(CurrentPos.X, testY));
}

void URouteWidget::ActiveWidget(FIntPoint pos)
{
   UE_LOG(LogTemp, Warning, TEXT("ActiveWidget : %d, %d"), pos.Y, pos.X);
   if (pos.Y < 0) //초기 실행의 경우
   {
      for (int i = 0; i < routePointer->MaxX; i++)
      {
         if (routePointer->Levels[0].Sectors[i].Type != ESectorType::None)
         {
            NodeMap[FIntPoint(0, i)]->SetState(ERouteNodeState::Selectable);
         }
      }
   }
   else
   {
      activeConnectedWidgets(pos);
   }
}

void URouteWidget::ApplyVisited(const TArray<FIntPoint>& Visited)
{
   // NodeMap 키는 (Y, X) 인데 방문 좌표는 (X, Y) 라 뒤집어서 찾는다
   for (const FIntPoint& V : Visited)
   {
      if (URouteNode** Node = NodeMap.Find(FIntPoint(V.Y, V.X)))
      {
         if (*Node)
         {
            (*Node)->SetState(ERouteNodeState::Visited);
         }
      }
   }
}

void URouteWidget::activeConnectedWidgets(FIntPoint pos)
{
   // idx는 이미 다음 층의 절대 X 좌표를 의미하므로, 변수명을 nextX로 생각하시면 이해가 편합니다.
   for (int nextX : routePointer->Levels[pos.Y].Sectors[pos.X].ConnectedSectors)
   {
      UE_LOG(LogTemp, Warning, TEXT("ActiveWidget : %d, %d"), pos.Y + 1, nextX);
      
      FIntPoint NextNodePos = FIntPoint(pos.Y + 1, nextX);
      
      // NodeMap에 해당 키가 존재하는지(유효한지) 안전하게 체크 후 Active
      if (NodeMap.Contains(NextNodePos) && NodeMap[NextNodePos] != nullptr)
      {
         NodeMap[NextNodePos]->SetState(ERouteNodeState::Selectable);
      }
      else
      {
         UE_LOG(LogTemp, Error, TEXT("NodeMap does not contain widget for %d, %d!"), NextNodePos.Y, NextNodePos.X);
      }
   }
}

// void URouteWidget::activeConnectedWidgets(FIntPoint pos)
// {
//    for (int idx : routePointer->Levels[pos.Y].Sectors[pos.X].ConnectedSectors)
//    {
//       UE_LOG(LogTemp, Warning, TEXT("ActiveWidget : %d, %d"), pos.Y + 1, pos.X + idx);
//       NodeMap[FIntPoint(pos.Y + 1, pos.X + idx)]->ActiveWidget();
//    }
// }

//위젯 생성 ============================================================
void URouteWidget::generateWidgets()
{
    if (!NodeContainer || !RouteNodeClass)
    {
       return;
    }
    
    NodePositions.SetNum(routePointer->MaxY);
    
    for (int i = 0; i < routePointer->MaxY; i++)
    {
       NodePositions[i].SetNum(routePointer->MaxX);
       
       for (int j = 0; j < routePointer->MaxX; j++)
       {
          const FVirSector& CurrentSector = routePointer->Levels[i].Sectors[j];
          
          // 1. None 타입 예외 처리: 위치만 계산하고 위젯은 만들지 않음
          if (CurrentSector.Type == ESectorType::None)
          {
             NodePositions[i][j] = GetNodePosition(i, j);
             continue;
          }
          
          URouteNode* Node = CreateWidget<URouteNode>(this, RouteNodeClass);
          NodeMap.Add(FIntPoint(i, j), Node);
          Node->Init(CurrentSector.Type, FIntPoint(j, i));
          
          // 노드 위치에 노이즈(지터) 적용
          FVector2D BasePos = GetNodePosition(i, j);
          FVector2D Jitter(
             LayoutRand.FRandRange(-nodeNoiseRange, nodeNoiseRange),
             LayoutRand.FRandRange(-nodeNoiseRange, nodeNoiseRange)
          );
          FVector2D FinalPos = BasePos + Jitter;
          NodePositions[i][j] = FinalPos;
          
          // CanvasPanel에 추가 및 슬롯 설정
          UCanvasPanelSlot* NodeSlot = Cast<UCanvasPanelSlot>(NodeContainer->AddChild(Node));
          if (NodeSlot)
          {
             NodeSlot->SetAnchors(FAnchors(0.5f, 0.5f)); // 중앙 기준
             NodeSlot->SetAlignment(FVector2D(0.5f, 0.5f)); // 위젯 피벗 중앙
             NodeSlot->SetPosition(FinalPos);
             NodeSlot->SetSize(NodeSize);
             NodeSlot->SetZOrder(1); // 점(EdgeDot)보다 위에 렌더링되도록 Z-Order 설정
          }
       }
    }
}

void URouteWidget::generateEdges()
{
    if (!NodeContainer || !EdgeDotClass)
    {
       return;
    }
    
    for (int i = 0; i < routePointer->MaxY; i++)
    {
       for (int j = 0; j < routePointer->MaxX; j++)
       {
          const FVirSector& CurrentSector = routePointer->Levels[i].Sectors[j];
          
          // 2. 시작 노드가 None이면 간선을 그리지 않음
          if (CurrentSector.Type == ESectorType::None)
          {
             continue;
          }
          
          for (int32 NextX : CurrentSector.ConnectedSectors)
          {
             // 인덱스 유효성 검사
             if (!NodePositions.IsValidIndex(i + 1) || !NodePositions[i + 1].IsValidIndex(NextX))
             {
                continue;
             }
             
             // 3. 목적지 노드가 None이면 간선을 그리지 않음
             if (routePointer->Levels[i + 1].Sectors[NextX].Type == ESectorType::None)
             {
                continue;
             }
             
             FVector2D Start = NodePositions[i][j];
             FVector2D End = NodePositions[i + 1][NextX];
             
             SpawnEdgeDots(Start, End, FIntPoint(i, j), FIntPoint(i + 1, NextX));
          }
       }
    }
}

void URouteWidget::SpawnEdgeDots(const FVector2D& Start, const FVector2D& End, FIntPoint StartCoord, FIntPoint EndCoord)
{
    FVector2D Delta = End - Start;
    float Length = Delta.Size();
    
    if (Length <= KINDA_SMALL_NUMBER)
    {
       return;
    }
    
    FVector2D Direction = Delta / Length;
    FVector2D Perpendicular(-Direction.Y, Direction.X);
    
    // 점 이미지가 길쭉한 선 모양일 경우를 대비해, 목적지를 향하는 각도(회전값)를 구합니다.
    float AngleRadians = FMath::Atan2(Direction.Y, Direction.X);
    float AngleDegrees = FMath::RadiansToDegrees(AngleRadians);
    
    int32 PointCount = FMath::Max(2, FMath::RoundToInt(Length / dotSpacing));
    
    TPair<FIntPoint,FIntPoint> EdgeKey(StartCoord, EndCoord);
   
    // 양 끝점(p=0, p=PointCount)은 노드에 겹치므로 제외하거나, 필요에 따라 범위를 조정하세요.
    // 여기서는 자연스러운 연결을 위해 p=1 부터 PointCount-1 까지만 그립니다.
    for (int32 p = 1; p < PointCount; p++)
    {
       float t = (float)p / (float)PointCount;
       FVector2D BasePoint = FMath::Lerp(Start, End, t);
       
       // 노이즈(Jitter) 적용 - 양 끝 노드에 가까울수록 노이즈 감소
       float fadeWeight = FMath::Sin(t * PI); 
       float offset = LayoutRand.FRandRange(-edgeNoiseRange, edgeNoiseRange) * fadeWeight;
       FVector2D FinalPoint = BasePoint + Perpendicular * offset;
       
       UUserWidget* Dot = CreateWidget<UUserWidget>(this, EdgeDotClass);
       if (!Dot) continue;

       // 회전 적용 (위젯 자체의 트랜스폼 회전)
       Dot->SetRenderTransformAngle(AngleDegrees);
       
       UCanvasPanelSlot* DotSlot = Cast<UCanvasPanelSlot>(NodeContainer->AddChild(Dot));
       if (DotSlot)
       {
          DotSlot->SetAnchors(FAnchors(0.5f, 0.5f)); // 중앙 기준
          DotSlot->SetAlignment(FVector2D(0.5f, 0.5f)); // 위젯 피벗 중앙
          DotSlot->SetPosition(FinalPoint);
          DotSlot->SetSize(DotSize);
          DotSlot->SetZOrder(0); // 노드보다 아래에 깔리도록 Z-Order 설정
       }
       
       EdgeMap.FindOrAdd(EdgeKey).Add(Dot);
    }
}

//인게임 ui 관련 ===============================================
void URouteWidget::VisibleUI(FIntPoint pos)
{
   SetPosition(pos.Y);
   SetVisibility(ESlateVisibility::Visible);

   if (OpenAnim)
   {
      PlayAnimation(OpenAnim);
   }
}

void URouteWidget::CloseUI(FIntPoint SelectedPos)
{
   if (bClosing)
   {
      return;
   }
   bClosing = true;
   PendingSelect = SelectedPos;

   // 애니메이션이 없으면 기다릴 것도 없다
   if (!CloseAnim)
   {
      OnCloseAnimFinished();
      return;
   }

   PlayAnimation(CloseAnim);
}

void URouteWidget::OnCloseAnimFinished()
{
   SetVisibility(ESlateVisibility::Hidden);
   bClosing = false;

   // 여기서 보내야 닫히는 연출이 끝난 뒤에 트래블이 시작된다
   if (ABasePlayerController* PC = Cast<ABasePlayerController>(GetOwningPlayer()))
   {
      PC->Server_SelectNode(PendingSelect);
   }
}

void URouteWidget::NativeConstruct()
{
   Super::NativeConstruct();

   // 재생이 끝난 뒤에 숨긴다. 먼저 숨기면 애니메이션이 한 프레임도 안 보인다
   if (CloseAnim)
   {
      FWidgetAnimationDynamicEvent Finished;
      Finished.BindDynamic(this, &URouteWidget::OnCloseAnimFinished);
      BindToAnimationFinished(CloseAnim, Finished);
   }

   SetVisibility(ESlateVisibility::Hidden);
}

FVector2D URouteWidget::GetNodePosition(int32 Y, int32 X) const
{
   float BaseX = (X * nodeSpacing) - (TotalWidth * 0.5f); 
   float BaseY = (TotalHeight * 0.5f) - (Y * levelDistance);
    
   return FVector2D(BaseX, BaseY);
}

//마우스 ============================================================

FReply URouteWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
   if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
   {
      bIsDragging = true;
      return FReply::Handled().CaptureMouse(TakeWidget());
   }
   return FReply::Unhandled();
}

FReply URouteWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
   if (bIsDragging)
   {
      UCanvasPanelSlot* ContainerSlot = Cast<UCanvasPanelSlot>(NodeContainer->Slot);
      if (ContainerSlot && routePointer)
      {
         FVector2D CursorDelta = InMouseEvent.GetCursorDelta();
         FVector2D CurrentPos = ContainerSlot->GetPosition();
            
         float MaxScroll = FMath::Max(0.f, TotalHeight);
         float NewY = FMath::Clamp(CurrentPos.Y + CursorDelta.Y, -MaxScroll, 0.f);
            
         ContainerSlot->SetPosition(FVector2D(CurrentPos.X, NewY));
      }
      return FReply::Handled();
   }
   return FReply::Unhandled();
}

FReply URouteWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
   if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
   {
      bIsDragging = false;
      return FReply::Handled().ReleaseMouseCapture();
   }
   return FReply::Unhandled();
}