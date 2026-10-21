#include "Sector.h"

#include "Door.h"
#include "Algo/Sort.h"
#include "Components/MeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameInstance.h"
#include "PCGMap/MapBuild/SectorBuild/SectorSubsystem.h"

namespace
{
	// 볼륨 촘촘함 비교용. 0보다 크면 모든 조각의 MaxVolumes 를 덮어쓴다
	TAutoConsoleVariable<int32> CVarSectorMaxVolumes(
		TEXT("sector.MaxVolumes"), 0,
		TEXT("0보다 크면 조각의 MaxVolumes 대신 이 값을 쓴다. 0이면 조각 설정 그대로"));

	/*
	 * 가장 긴 축의 중앙값으로 갈라 내려가며 잎마다 박스를 하나씩 남긴다.
	 * BVH를 세운 뒤 고정 깊이에서 자른 것과 결과가 같아서 트리를 따로 들고 있지 않는다.
	 */
	void SplitVolumes(TArrayView<FBox> Prims, int32 Depth, TArray<FBox>& Out)
	{
		FBox Node(ForceInit);
		for (const FBox& Prim : Prims)
		{
			Node += Prim;
		}

		if (Depth <= 0 || Prims.Num() <= 1)
		{
			Out.Add(Node);
			return;
		}

		const FVector Size = Node.GetSize();
		const int32 Axis = Size.X > Size.Y
			? (Size.X > Size.Z ? 0 : 2)
			: (Size.Y > Size.Z ? 1 : 2);

		Algo::Sort(Prims, [Axis](const FBox& A, const FBox& B)
		{
			return A.GetCenter()[Axis] < B.GetCenter()[Axis];
		});

		const int32 Mid = Prims.Num() / 2;
		SplitVolumes(Prims.Left(Mid), Depth - 1, Out);
		SplitVolumes(Prims.RightChop(Mid), Depth - 1, Out);
	}
}

ASector::ASector()
{
	PrimaryActorTick.bCanEverTick = false;

	/*
	 * 서버가 놓고 클라는 복제로 받는다. 머신마다 따로 스폰하면 두 액터가 서로 남남이라,
	 * 플레이어가 밟고 선 바닥을 클라가 못 찾고 서버의 위치 보정을 통째로 버린다
	 */
	bReplicates = true;

	/*
	 * 조각은 레벨 그 자체다. 거리로 걸러지면 안 된다 —
	 * 노드 사이가 수만 유닛이라, 로비에 선 클라에게는 다음 방이 통째로 안 간다.
	 * 그러면 그 바닥을 밟은 폰의 위치 보정도 클라가 풀 수 없어 서로 물린다
	 */
	bAlwaysRelevant = true;
}

void ASector::BeginPlay()
{
	Super::BeginPlay();

	// 서버 것은 SpawnPlacements 가 목록에 넣는다. 클라는 하나씩 도착하므로 스스로 알린다
	if (GetNetMode() != NM_Client)
	{
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->GetSubsystem<USectorSubsystem>()->RegisterReplicatedSector(this);
	}
}

void ASector::Init()
{
	Doors.Reset();
	Volumes.Reset();

	// 루트 스케일(1.3, 1.5 등)까지 나눠 버리면 잰 값이 실제 크기와 어긋나 붙일 자리가 틀어진다.
	// 위치·회전만 기준으로 재서 스케일은 크기에 남긴다. 화살표/소켓은 UMeshComponent가 아니라 안 걸린다
	TArray<FBox> Prims;
	const FTransform ActorFrame(GetActorQuat(), GetActorLocation());

	for (UMeshComponent* Mesh : TInlineComponentArray<UMeshComponent*>(this))
	{
		const FTransform ToLocal = Mesh->GetComponentTransform().GetRelativeTransform(ActorFrame);
		const FBox Box = Mesh->CalcBounds(ToLocal).GetBox();

		// 메시를 안 꽂은 컴포넌트는 CalcBounds 가 제자리에 크기 0 박스를 뱉는다. 섞으면 볼륨이 그쪽으로 늘어난다
		if (!Box.GetSize().IsNearlyZero())
		{
			Prims.Add(Box);
		}
	}

	if (Prims.Num() > 0)
	{
		const int32 Override = CVarSectorMaxVolumes.GetValueOnAnyThread();
		const int32 Cap = Override > 0 ? Override : MaxVolumes;

		SplitVolumes(Prims, FMath::CeilLogTwo(FMath::Max(Cap, 1)), Volumes);
	}

	TArray<AActor*> Attached;
	//부착된 전체 액터를 가져온다 -> door cast가 성공한 대상만 door ptr에 등록한다
	GetAttachedActors(Attached);

	for (AActor* Actor : Attached)
	{
		ADoor* Door = Cast<ADoor>(Actor);
		if (!Door)
		{
			continue;
		}

		FSectorDoorSlot& Slot = Doors.AddDefaulted_GetRef();
		Slot.Door = Door;

		const FTransform DoorFrame(Door->GetActorQuat(), Door->GetActorLocation());
		for (UMeshComponent* Mesh : TInlineComponentArray<UMeshComponent*>(Door))
		{
			const FBox Box = Mesh->CalcBounds(
				Mesh->GetComponentTransform().GetRelativeTransform(DoorFrame)).GetBox();

			// 빈 컴포넌트의 크기 0 박스가 섞이면 맞물릴 면이 그만큼 밀린다
			if (!Box.GetSize().IsNearlyZero())
			{
				Slot.MeshBounds += Box;
			}
		}
	}
}
