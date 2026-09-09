#include "RouteNode.h"

#include "RouteWidget.h"
#include "GameCore/BasePlayerController.h"

void URouteNode::Init(ESectorType Type, FIntPoint Pos)
{
	nodePos = Pos;
	SectorType = Type;

	// 기본은 잠김. 갈 수 있는 곳과 지나온 곳은 RouteWidget 이 따로 켠다
	SetState(ERouteNodeState::Locked);
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

	// 서버 전송은 위젯이 닫히는 애니메이션을 끝낸 뒤에 한다
	if (URouteWidget* Owner = GetTypedOuter<URouteWidget>())
	{
		Owner->CloseUI(nodePos);
	}
}
