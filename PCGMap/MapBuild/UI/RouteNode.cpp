#include "RouteNode.h"

#include "RouteWidget.h"
#include "GameCore/BasePlayerController.h"
#include "GameCore/BaseGameState.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "UObject/ConstructorHelpers.h"

URouteNode::URouteNode(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> Texture(TEXT("/Game/TPSDevelop/UI/RouteSelect/Resource/vote.vote"));
	checkf(Texture.Succeeded(), TEXT("[RouteVote] 투표 아이콘 에셋이 없습니다."));
	VoteIcon.SetResourceObject(Texture.Object);
	VoteIcon.ImageSize = FVector2D(14.f, 14.f);
	VoteIcon.DrawAs = ESlateBrushDrawType::Image;
}

void URouteNode::NativeConstruct()
{
	Super::NativeConstruct();
	VoteGameState = GetWorld()->GetGameState<ABaseGameState>();
	check(VoteGameState.IsValid());
	VoteGameState->OnRouteVotesChanged.AddUniqueDynamic(this, &URouteNode::OnVotesChanged);
	RefreshVotes();
}

void URouteNode::NativeDestruct()
{
	if (VoteGameState.IsValid())
	{
		VoteGameState->OnRouteVotesChanged.RemoveDynamic(this, &URouteNode::OnVotesChanged);
	}
	Super::NativeDestruct();
}

void URouteNode::RefreshVotes()
{
	const URouteWidget* Route = GetTypedOuter<URouteWidget>();
	VoteCount = Route && VoteGameState.IsValid() && VoteGameState->GetRouteVote().Round == Route->GetVoteRound()
		? VoteGameState->GetRouteVote().CountVotes(nodePos) : 0;
	InvalidateLayoutAndVolatility();
}

void URouteNode::OnVotesChanged(FIntPoint Node, int32 Count)
{
	const URouteWidget* Route = GetTypedOuter<URouteWidget>();
	if (Node == nodePos && Route && VoteGameState.IsValid() && VoteGameState->GetRouteVote().Round == Route->GetVoteRound())
	{
		VoteCount = Count;
		InvalidateLayoutAndVolatility();
	}
}

int32 URouteNode::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 TopLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size(VoteIcon.ImageSize);
	const float Width = VoteCount * (Size.X + 2.f) - 2.f;
	const FVector2D Start((AllottedGeometry.GetLocalSize().X - Width) * 0.5f, AllottedGeometry.GetLocalSize().Y + 4.f);
	for (int32 Index = 0; Index < VoteCount; ++Index)
	{
		const FVector2D Position = Start + FVector2D(Index * (Size.X + 2.f), 0.f);
		FSlateDrawElement::MakeBox(OutDrawElements, TopLayer + 1,
			AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), &VoteIcon,
			ESlateDrawEffect::None, VoteIcon.GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint());
	}
	return VoteCount > 0 ? TopLayer + 1 : TopLayer;
}

void URouteNode::Init(ESectorType Type, FIntPoint Pos)
{
	nodePos = Pos;
	SectorType = Type;

	// 기본은 잠김. 갈 수 있는 곳과 지나온 곳은 RouteWidget 이 따로 켠다
	SetState(ERouteNodeState::Locked);
	RefreshVotes();
}

void URouteNode::SetState(ERouteNodeState NewState)
{
	NodeState = NewState;
	OnTextureSet(SectorType, NodeState);
}

void URouteNode::Select()
{
	// 갈 수 없는 노드는 눌러도 무시한다
	if (NodeState != ERouteNodeState::Selectable)
	{
		return;
	}

	if (URouteWidget* Owner = GetTypedOuter<URouteWidget>())
	{
		Owner->VoteForNode(nodePos);
	}
}
