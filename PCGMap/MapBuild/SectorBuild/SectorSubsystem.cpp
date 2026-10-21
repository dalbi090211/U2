#include "SectorSubsystem.h"

#include "Sector/SectorStatic.h"
#include "Sector/Struct/Door.h"
#include "Sector/Struct/StartSector.h"
#include "GameFramework/PlayerStart.h"
#include "PCGMap/MapBuild/MapStatic.h"

namespace
{
	const FVector ProbeLocation(0.f, 0.f, -200000.f);

	// 한 번에 좋은 모양이 나오는 일이 드물다. 이만큼 지어 보고 점수가 제일 높은 것만 쓴다
	constexpr int32 RouteTries = 5;

	/*
	 * 루트 점수 가중치. 값 자체보다 서로의 비율이 결과를 정한다.
	 * 넓이는 제곱미터라 수백 단위, 나머지는 이음/칸 하나당이라 수십 단위로 맞춰 두었다
	 */
	constexpr float AreaWeight = 1.f;
	constexpr float AlternateBonus = 40.f;
	constexpr float LongBranchPenalty = 25.f;

	// 이 칸 수까지는 걸어다닐 만하다고 본다. 넘는 칸마다 감점
	constexpr int32 ComfortableDepth = 5;

	// 두 방의 부피가 이 배 이상 차이나면 "큰 방 다음 작은 방"으로 친다
	constexpr float SizeStepRatio = 1.6f;
	constexpr float EntryLift = 120.f;
	constexpr int32 SearchBudget = 20000;

	// 조각 루트 스케일(1.3, 1.5…)을 나눠 버리면 잰 값이 실제 크기와 어긋난다. 위치·회전만 기준으로 쓴다
	FTransform ActorFrame(const AActor* Actor)
	{
		return FTransform(Actor->GetActorQuat(), Actor->GetActorLocation());
	}

	/*
	 * 두 문이 맞닿는 접점 — 문 메시 앞면(+X)의 한가운데, 바닥 높이. 문 액터 로컬.
	 * 피벗은 문틀 한쪽 끝에 있어서 피벗끼리 맞대면 문 폭만큼 옆으로 어긋난다
	 */
	FVector DoorAnchor(const FBox& MeshBounds)
	{
		return FVector(MeshBounds.Max.X, MeshBounds.GetCenter().Y, MeshBounds.Min.Z);
	}

	// 문 하나의 접점을 월드로 올린다. 스케일은 늘 1
	FTransform DoorContact(const ADoor* Door, const FBox& MeshBounds)
	{
		const FTransform Frame = ActorFrame(Door);
		return FTransform(Frame.GetRotation(), Frame.TransformPosition(DoorAnchor(MeshBounds)));
	}

	/*
	 * 놓인 볼륨 둘이 겹치는가. 서로의 로컬 좌표로 옮겨 양쪽에서 본다.
	 * 세워 놓고 요만 도는 상자끼리는 이 두 번으로 정확하다 — 둘을 갈라놓을 수 있는 면이 서로의 옆면뿐이라서.
	 * 기울어진 조각이 섞이면 더 겹쳤다고 볼 뿐, 겹친 것을 놓치지는 않는다
	 */
	bool VolumesOverlap(const FSectorVolume& A, const FSectorVolume& B)
	{
		return DoAABB(A.Local, B.Local.TransformBy(B.Frame * A.Frame.Inverse()))
			&& DoAABB(B.Local, A.Local.TransformBy(A.Frame * B.Frame.Inverse()));
	}

	// 부피가 확 차이나는 짝인가. 큰 방만 이어지면 지나다니기 피곤하다
	bool IsSizeStep(const FBox& A, const FBox& B)
	{
		const double Small = FMath::Min(A.GetVolume(), B.GetVolume());
		const double Big = FMath::Max(A.GetVolume(), B.GetVolume());

		return Small > 0.0 && Big > Small * SizeStepRatio;
	}

	/*
	 * 시작에서 먼 것들 중 서로도 멀리 떨어진 Count 개. 제일 먼 것을 하나 잡고,
	 * 그다음부터는 이미 고른 것들과 가장 멀리 떨어진 것을 고른다.
	 * 깊이만 보고 고르면 끝 방이 한쪽 가지에 몰려서 고른 티가 안 난다
	 */
	void PickSpreadFarNodes(const TArray<FSectorPlacement>& Placements, const FSectorGraph& Graph,
							int32 Count, TArray<int32>& Out)
	{
		Out.Reset();
		if (Placements.Num() <= 1 || Count <= 0)
		{
			return;
		}

		int32 MaxDepth = 0;
		for (const int32 Depth : Graph.Depth)
		{
			MaxDepth = FMath::Max(MaxDepth, Depth);
		}

		// 딱 최대 깊이만 보면 후보가 하나뿐일 때가 많다. 한 칸 안쪽까지 넣는다
		TArray<int32> Pool;
		for (int32 i = 1; i < Placements.Num(); i++)
		{
			if (Graph.Depth[i] >= MaxDepth - 1)
			{
				Pool.Add(i);
			}
		}

		const FVector Start = Placements[0].Transform.GetLocation();

		while (Out.Num() < Count && Pool.Num() > 0)
		{
			int32 BestAt = 0;
			double BestValue = -1.0;

			for (int32 p = 0; p < Pool.Num(); p++)
			{
				const FVector Pos = Placements[Pool[p]].Transform.GetLocation();

				double Value = FVector::Dist(Pos, Start);
				for (const int32 Chosen : Out)
				{
					Value = FMath::Min(Value,
						FVector::Dist(Pos, Placements[Chosen].Transform.GetLocation()));
				}

				if (Value > BestValue)
				{
					BestValue = Value;
					BestAt = p;
				}
			}

			Out.Add(Pool[BestAt]);
			Pool.RemoveAt(BestAt);
		}
	}

	// 두 문의 접점이 마주 붙도록 새 조각을 놓을 액터 트랜스폼
	FTransform movePos(const FSectorDoor& OpenDoor, const FSectorDoor& NewDoor)
	{
		// 열린 문의 접점을 180도 돌린 자리에 새 조각의 문 접점을 얹는다. 두 문이 앞면끼리 맞닿는다
		FTransform Target = OpenDoor.Transform;
		Target.SetRotation(Target.GetRotation() * FQuat(FVector::UpVector, PI));

		return NewDoor.Transform.Inverse() * Target;
	}
}

void USectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadPieceSets();
}

void USectorSubsystem::LoadPieceSets()
{
	PieceSets.Reset();

	const UEnum* EnvEnum = StaticEnum<EEnvironmentType>();
	for (int32 i = 0; i < EnvEnum->NumEnums() - 1; i++)
	{
		const FString AssetName = SectorPieceSetPrefix + EnvEnum->GetNameStringByIndex(i);
		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *SectorPieceSetDir, *AssetName, *AssetName);

		if (USectorPieceSet* Set = LoadObject<USectorPieceSet>(nullptr, *ObjectPath))
		{
			PieceSets.Add(static_cast<EEnvironmentType>(EnvEnum->GetValueByIndex(i)), Set);
		}
	}
}

TArray<FSectorPlacement> USectorSubsystem::BuildSectors(UWorld* World, EEnvironmentType Env, const FTransform& Origin,
														const FWorldGenSheet& Sheet)
{
	TArray<FSectorPlacement> Best;
	float BestScore = -FLT_MAX;

	for (int32 Try = 0; Try < RouteTries; Try++)
	{
		TArray<FSectorPlacement> Route = buildOneRoute(World, Env, Origin, Sheet);
		const float Score = scoreRoute(Route);

		if (Score > BestScore)
		{
			BestScore = Score;
			Best = MoveTemp(Route);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Sector] 루트 %d개 중 선택 — 조각 %d개, 점수 %.0f"),
		   RouteTries, Best.Num(), BestScore);

	return Best;
}

TArray<FSectorPlacement> USectorSubsystem::buildOneRoute(UWorld* World, EEnvironmentType Env,
														 const FTransform& Origin, const FWorldGenSheet& Sheet)
{
	FSectorBuildContext Ctx;
	Ctx.World = World;
	// 끝 방은 본체를 다 지은 뒤 이 한도와 상관없이 붙는다. 미리 빼 둬야 합계를 넘지 않는다
	Ctx.TotalCount = Sheet.SectorPieceCount - Sheet.EndSectorCount;
	// 안 끊으면 깊이 우선 탐색이 조각을 다 먹어 갈림길이 빈손이 된다
	Ctx.MaxRouteLength = Sheet.MaxRouteLength;
	Ctx.EndSectorCount = Sheet.EndSectorCount;
	Ctx.Budget = SearchBudget;
	setPieceSet(Ctx.PieceSet, Env);
	setPieceRules(Ctx.Rules);

	// 첫 조각은 붙일 문이 없다. 원점에 DoorType None 인 문을 세워 시작한다
	FSectorDoor OpenDoor;
	OpenDoor.Transform = Origin;

	TArray<FSectorPlacement> Result;
	//덜 만들어져도 내보낸다. 방 개수를 꼭 맞출 필요 없음
	RecurBuildSector(Ctx, OpenDoor, TOptional<ESectorPieceType>(), 1, Result);

	attachEndSectors(Ctx, Result);

	return Result;
}

void USectorSubsystem::setPieceSet(USectorPieceSet*& set, EEnvironmentType Env)
{
	set = PieceSets.FindRef(Env);

	// 등록이 빠졌으면 여기서 죽어야 한다. 널로 넘기면 한참 뒤 엉뚱한 데서 터진다
	checkf(set, TEXT("[Sector] 조각 묶음 없음: Env=%d"), (int32)Env);
}

void USectorSubsystem::setPieceRules(TArray<TUniquePtr<FSectorRule>>& Rules)
{
	Rules.Reset();
	Rules.Add(MakeUnique<FFirstSectorRule>());
	Rules.Add(MakeUnique<FBanContInterRule>());
	Rules.Add(MakeUnique<FBanContConnectRule>());
}

void USectorSubsystem::RecurBuildSector(FSectorBuildContext& Ctx, const FSectorDoor& OpenDoor,
									   const TOptional<ESectorPieceType>& PrevType, int32 Depth,
									   TArray<FSectorPlacement>& Result)
{
	if (--Ctx.Budget <= 0 || Ctx.TotalCount == 0 || Depth > Ctx.MaxRouteLength)
	{
		return;
	}

	TArray<FSectorShape> Candidates;
	EvaluateCandidates(Ctx, OpenDoor.DoorType, PrevType, Candidates);

	for (const FSectorShape& Cand : Candidates)
	{
		int32 EntryDoor = INDEX_NONE;
		FTransform Placed;
		if (!tryPlace(Cand, OpenDoor, Ctx, EntryDoor, Placed))	//성공 시 Placed에 반환
		{
			continue;
		}

		commitPlace(Ctx, Cand, Placed, Result);
		branchFromDoors(Ctx, Cand, Placed, Depth, EntryDoor, Result);
		return;
	}
}

void USectorSubsystem::commitPlace(FSectorBuildContext& Ctx, const FSectorShape& Shape,
								   const FTransform& Placed, TArray<FSectorPlacement>& Result)
{
	FSectorPlacement& Out = Result.AddDefaulted_GetRef();
	Out.SpawnClass = Shape.Piece.Get();
	Out.Transform = Placed;

	for (const FBox& Local : Shape.Volumes)
	{
		Ctx.Occupied.Emplace(Local, Placed);
	}

	Ctx.TotalCount--;
}

void USectorSubsystem::branchFromDoors(FSectorBuildContext& Ctx, const FSectorShape& Shape,
									   const FTransform& Placed, int32 Depth, int32 EntryDoor,
									   TArray<FSectorPlacement>& Result)
{
	for (int32 d = 0; d < Shape.Doors.Num() && Ctx.TotalCount > 0; d++)
	{
		if (d == EntryDoor)
		{
			continue;
		}

		// 조각 로컬인 문을 월드로 올려 넘긴다
		FSectorDoor NextDoor = Shape.Doors[d];
		NextDoor.Transform = Shape.Doors[d].Transform * Placed;

		// 이 문으로 들어가는 플레이어에게는 방금 놓은 조각이 직전 방이다
		RecurBuildSector(Ctx, NextDoor, Shape.Type, Depth + 1, Result);
	}
}

void USectorSubsystem::EvaluateCandidates(FSectorBuildContext& Ctx, EDoorShapeType DoorType,
										  const TOptional<ESectorPieceType>& PrevType,
										  TArray<FSectorShape>& Out)
{
	// 자리마다 새로 뜬다. 룰이 고친 값이 다음 자리로 안 넘어간다
	TArray<float> Weights;
	Weights.Init(1.f, StaticEnum<ESectorPieceType>()->NumEnums() - 1);

	// 시작/끝은 아무 데나 끼면 안 된다. 룰이 올려 줄 때만 나온다
	Weights[(int32)ESectorPieceType::Start] = 0.f;
	Weights[(int32)ESectorPieceType::End] = 0.f;

	for (const TUniquePtr<FSectorRule>& Rule : Ctx.Rules)
	{
		Rule->Execute(Ctx.TotalCount, PrevType, Weights);
	}

	// ponytail: 확률 안에서 고르는 건 아직 랜덤. 고를 기준이 생기면 여기만 고친다
	// EnsureShape 가 캐시를 늘리며 재해시하므로 포인터가 아니라 값으로 든다
	TArray<TPair<FSectorShape, float>> Scored;

	for (const TPair<ESectorPieceType, FSectorPieceList>& Pair : Ctx.PieceSet->Pieces)
	{
		const float Weight = Weights[(int32)Pair.Key];
		if (Weight <= 0.f)
		{
			continue;
		}

		for (const TSoftClassPtr<ASector>& Soft : Pair.Value.Pieces)
		{
			const FSectorShape* Shape = EnsureShape(Ctx.World, Soft.LoadSynchronous(), Pair.Key);
			if (Shape->HasDoor(DoorType))
			{
				Scored.Emplace(*Shape, randStream.FRand() * Weight);
			}
		}
	}

	Scored.Sort([](const TPair<FSectorShape, float>& A,
				   const TPair<FSectorShape, float>& B) { return A.Value > B.Value; });

	Out.Reset();
	Out.Reserve(Scored.Num());
	for (TPair<FSectorShape, float>& Entry : Scored)
	{
		Out.Add(MoveTemp(Entry.Key));
	}
}

const FSectorShape* USectorSubsystem::EnsureShape(UWorld* World, TSubclassOf<ASector> SectorClass,
												  ESectorPieceType Type)
{
	if (const FSectorShape* Cached = ShapeCache.Find(SectorClass))
	{
		return Cached;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASector* Probe = World->SpawnActor<ASector>(SectorClass, ProbeLocation, FRotator::ZeroRotator, Params);
	checkf(Probe, TEXT("[Sector] 프로브 스폰 실패: %s"), *GetNameSafe(SectorClass));

	Probe->Init();

	FSectorShape Shape;
	Shape.Piece = SectorClass;
	Shape.Type = Type;
	Shape.Volumes = Probe->Volumes;

	const FTransform ProbeFrame = ActorFrame(Probe);
	for (const FSectorDoorSlot& Slot : Probe->Doors)
	{
		Shape.Doors.Add(makeDoor(Slot, ProbeFrame));
	}

	Probe->Destroy();

	checkf(Shape.IsValid(), TEXT("[Sector] 문이 없다: %s"), *GetNameSafe(SectorClass));

	return &ShapeCache.Add(SectorClass, MoveTemp(Shape));
}

FSectorDoor USectorSubsystem::makeDoor(const FSectorDoorSlot& Slot, const FTransform& SectorFrame)
{
	// 메시를 못 재면 접점이 피벗으로 주저앉아 조각이 문 폭만큼 어긋난 채 조립된다
	checkf(Slot.MeshBounds.IsValid, TEXT("[Sector] 문 메시 없음: %s"), *GetNameSafe(Slot.Door));

	FSectorDoor Door;
	// 문 forward(+X) 가 섹터 바깥을 본다는 것이 문 배치의 약속이다. 섹터 모양으로 추측하지 않는다
	Door.Transform = DoorContact(Slot.Door, Slot.MeshBounds).GetRelativeTransform(SectorFrame);
	Door.DoorType = Slot.Door->DoorType;

	return Door;
}

bool USectorSubsystem::tryPlace(const FSectorShape& PlaceSector, const FSectorDoor& OpenDoor,
							   const FSectorBuildContext& Ctx,
							   int32& OutEntryDoor, FTransform& OutTransform) const
{
	for (int32 d = 0; d < PlaceSector.Doors.Num(); d++)
	{
		if (!PlaceSector.Doors[d].FitsInto(OpenDoor))
		{
			continue;
		}

		const FTransform Placement = movePos(OpenDoor, PlaceSector.Doors[d]);
		if (OverlapsOccupied(PlaceSector, Placement, Ctx.Occupied))
		{
			continue;
		}

		OutEntryDoor = d;
		OutTransform = Placement;
		return true;
	}

	return false;
}

bool USectorSubsystem::OverlapsOccupied(const FSectorShape& Shape, const FTransform& Placement,
										const TArray<FSectorVolume>& Occupied)
{
	for (const FBox& Local : Shape.Volumes)
	{
		const FSectorVolume Candidate(Local, Placement);

		for (const FSectorVolume& Taken : Occupied)
		{
			if (VolumesOverlap(Candidate, Taken))
			{
				return true;
			}
		}
	}

	return false;
}

void USectorSubsystem::SpawnPlacements(UWorld* World, const TArray<FSectorPlacement>& Placements)
{
	// 조각은 복제 액터다. 클라는 스폰하지 않고 도착하는 것을 RegisterReplicatedSector 로 받는다
	if (World->GetNetMode() == NM_Client)
	{
		/*
		 * 조각이 이 배치 목록보다 먼저 도착하는 일이 흔하다. 통째로 비우면 먼저 온 것들을 잃고
		 * 개수가 영영 안 차서 문이 안 열린다. 서버가 지운 것만 걷어낸다
		 */
		SpawnedSectors.RemoveAll([](const TObjectPtr<ASector>& Sector) { return !IsValid(Sector); });

		ExpectedSectorCount = Placements.Num();
		openDoorsWhenAllArrived();
		return;
	}

	ClearSpawned();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FSectorPlacement& Placement : Placements)
	{
		UClass* SpawnClass = Placement.SpawnClass.LoadSynchronous();
		checkf(SpawnClass, TEXT("[Sector] 배치 클래스 로드 실패: %s"), *Placement.SpawnClass.ToString());

		ASector* Spawned = World->SpawnActor<ASector>(SpawnClass, Placement.Transform, Params);
		checkf(Spawned, TEXT("[Sector] 조각 스폰 실패: %s"), *SpawnClass->GetName());

		// SpawnActor 는 BP 루트에 박힌 상대 위치·회전을 스폰 트랜스폼에 곱해 넣는다. 문 좌표는
		// 프로브의 실제 액터 위치 기준으로 쟀으니, 되돌리지 않으면 그만큼 밀린 채 붙는다.
		// 스케일은 BP 값이 맞고 잰 크기에도 그 값이 들어가 있으므로 건드리지 않는다
		Spawned->SetActorLocationAndRotation(Placement.Transform.GetLocation(),
											 Placement.Transform.GetRotation());

		SpawnedSectors.Add(Spawned);
	}

	openConnectedDoors();

	// 배치가 비면(조립 실패) 직전 값을 그대로 둔다
	if (SpawnedSectors.Num() > 0)
	{
		EntryTransform = MakeEntryTransform(SpawnedSectors[0]);

		// 첫 조각은 FFirstSectorRule 이 시작 타입으로 고정한다. 시작 조각 BP 가 AStartSector 가 아니면 걸 이벤트가 없다
		if (AStartSector* Start = Cast<AStartSector>(SpawnedSectors[0]))
		{
			Start->ExecuteStartEvents();
		}
	}

	OnSectorReady.Broadcast();
}

const FSectorShape* USectorSubsystem::findShape(const FSectorPlacement& Placement) const
{
	UClass* Class = Placement.SpawnClass.Get();
	return Class ? ShapeCache.Find(Class) : nullptr;
}

void USectorSubsystem::makeGraph(const TArray<FSectorPlacement>& Placements, FSectorGraph& Out) const
{
	const int32 Num = Placements.Num();

	Out.Adjacent.Reset();
	Out.Adjacent.SetNum(Num);
	Out.Depth.Init(INDEX_NONE, Num);
	Out.Parent.Init(INDEX_NONE, Num);
	Out.FreeDoors.Reset();

	if (Num == 0)
	{
		return;
	}

	// 모든 문을 월드 접점으로 펼쳐 놓고 겹치는 짝을 찾는다. openConnectedDoors 와 같은 기준이다
	struct FDoorSlot
	{
		int32 Node;
		int32 Door;
		FVector Contact;
	};

	TArray<FDoorSlot> Slots;
	for (int32 i = 0; i < Num; i++)
	{
		const FSectorShape* Shape = findShape(Placements[i]);
		if (!Shape)
		{
			continue;
		}

		for (int32 d = 0; d < Shape->Doors.Num(); d++)
		{
			const FTransform World = Shape->Doors[d].Transform * Placements[i].Transform;
			Slots.Add({ i, d, World.GetLocation() });
		}
	}

	TArray<bool> Mated;
	Mated.Init(false, Slots.Num());

	for (int32 a = 0; a < Slots.Num(); a++)
	{
		for (int32 b = a + 1; b < Slots.Num(); b++)
		{
			if (FVector::DistSquared(Slots[a].Contact, Slots[b].Contact) > Tol * Tol)
			{
				continue;
			}

			Out.Adjacent[Slots[a].Node].AddUnique(Slots[b].Node);
			Out.Adjacent[Slots[b].Node].AddUnique(Slots[a].Node);
			Mated[a] = true;
			Mated[b] = true;
		}
	}

	for (int32 i = 0; i < Slots.Num(); i++)
	{
		if (!Mated[i])
		{
			Out.FreeDoors.Emplace(Slots[i].Node, Slots[i].Door);
		}
	}

	// 시작 조각에서 너비 우선. 여기서 나온 칸 수가 곧 플레이어가 걸어야 하는 방 수다
	TArray<int32> Queue;
	Queue.Add(0);
	Out.Depth[0] = 0;

	for (int32 Head = 0; Head < Queue.Num(); Head++)
	{
		for (const int32 Next : Out.Adjacent[Queue[Head]])
		{
			if (Out.Depth[Next] != INDEX_NONE)
			{
				continue;
			}

			Out.Depth[Next] = Out.Depth[Queue[Head]] + 1;
			Out.Parent[Next] = Queue[Head];
			Queue.Add(Next);
		}
	}
}

void USectorSubsystem::gatherPieces(FSectorBuildContext& Ctx, ESectorPieceType Type,
									EDoorShapeType DoorType, TArray<FSectorShape>& Out)
{
	Out.Reset();

	const FSectorPieceList* List = Ctx.PieceSet->Pieces.Find(Type);
	if (!List)
	{
		return;
	}

	for (const TSoftClassPtr<ASector>& Soft : List->Pieces)
	{
		const FSectorShape* Shape = EnsureShape(Ctx.World, Soft.LoadSynchronous(), Type);
		if (Shape->HasDoor(DoorType))
		{
			Out.Add(*Shape);
		}
	}
}

bool USectorSubsystem::tryAttachEnd(FSectorBuildContext& Ctx, int32 NodeIndex,
									const FSectorGraph& Graph, TArray<FSectorPlacement>& Result)
{
	const FSectorShape* Shape = Result.IsValidIndex(NodeIndex) ? findShape(Result[NodeIndex]) : nullptr;
	if (!Shape)
	{
		return false;
	}

	const FVector Outward = (Result[NodeIndex].Transform.GetLocation()
		- Result[0].Transform.GetLocation()).GetSafeNormal();

	// 빈 문을 시작에서 멀어지는 쪽부터 본다. 안쪽으로 난 문에 끝 방을 달면 길이 되돌아간다
	TArray<TPair<float, int32>> Doors;
	for (const TPair<int32, int32>& Free : Graph.FreeDoors)
	{
		if (Free.Key != NodeIndex)
		{
			continue;
		}

		const FTransform World = Shape->Doors[Free.Value].Transform * Result[NodeIndex].Transform;
		Doors.Emplace(FVector::DotProduct(World.GetRotation().GetForwardVector(), Outward), Free.Value);
	}

	Doors.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
	{
		return A.Key > B.Key;
	});

	for (const TPair<float, int32>& Entry : Doors)
	{
		FSectorDoor OpenDoor = Shape->Doors[Entry.Value];
		OpenDoor.Transform = OpenDoor.Transform * Result[NodeIndex].Transform;

		TArray<FSectorShape> Candidates;
		gatherPieces(Ctx, ESectorPieceType::End, OpenDoor.DoorType, Candidates);

		for (const FSectorShape& Cand : Candidates)
		{
			int32 EntryDoor = INDEX_NONE;
			FTransform Placed;
			if (!tryPlace(Cand, OpenDoor, Ctx, EntryDoor, Placed))
			{
				continue;
			}

			commitPlace(Ctx, Cand, Placed, Result);
			return true;
		}
	}

	return false;
}

void USectorSubsystem::attachEndSectors(FSectorBuildContext& Ctx, TArray<FSectorPlacement>& Result)
{
	if (Result.IsEmpty())
	{
		return;
	}

	FSectorGraph Graph;
	makeGraph(Result, Graph);

	TArray<int32> Targets;
	PickSpreadFarNodes(Result, Graph, Ctx.EndSectorCount, Targets);

	int32 Attached = 0;
	for (const int32 Node : Targets)
	{
		if (tryAttachEnd(Ctx, Node, Graph, Result))
		{
			Attached++;
			continue;
		}

		// 그 자리에 공간이 없으면 한 칸 되돌아가 다시 본다
		const int32 Back = Graph.Parent[Node];
		if (Back != INDEX_NONE && tryAttachEnd(Ctx, Back, Graph, Result))
		{
			Attached++;
		}
	}

	if (Attached > 0)
	{
		return;
	}

	/*
	 * 두 번 실패한 것은 그냥 넘기지만 하나도 못 붙인 것은 못 넘긴다 — 끝 방이 없으면 맵이 안 끝난다.
	 * 먼 조각부터 전부 훑어서 하나는 붙인다
	 */
	TArray<int32> ByDistance;
	for (int32 i = 1; i < Graph.Depth.Num(); i++)
	{
		if (Graph.Depth[i] != INDEX_NONE)
		{
			ByDistance.Add(i);
		}
	}

	ByDistance.Sort([&Graph](int32 A, int32 B) { return Graph.Depth[A] > Graph.Depth[B]; });

	for (const int32 Node : ByDistance)
	{
		if (tryAttachEnd(Ctx, Node, Graph, Result))
		{
			return;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[Sector] 끝 방을 붙일 자리가 없다 — 조각 %d개"), Result.Num());
}

float USectorSubsystem::scoreRoute(const TArray<FSectorPlacement>& Placements) const
{
	if (Placements.IsEmpty())
	{
		return -FLT_MAX;
	}

	FSectorGraph Graph;
	makeGraph(Placements, Graph);

	// 조각마다 덮는 상자. 전체 넓이와 크기 비교에 둘 다 쓴다
	TArray<FBox> Boxes;
	Boxes.Reserve(Placements.Num());
	FBox Whole(ForceInit);

	for (const FSectorPlacement& Placement : Placements)
	{
		FBox Box(ForceInit);
		if (const FSectorShape* Shape = findShape(Placement))
		{
			for (const FBox& Local : Shape->Volumes)
			{
				Box += Local.TransformBy(Placement.Transform);
			}
		}

		Boxes.Add(Box);
		Whole += Box;
	}

	// 넓이를 제곱미터로 줄인다. 센티미터 그대로 쓰면 자릿수가 커서 나머지 항이 묻힌다
	const FVector Size = Whole.GetSize();
	float Score = static_cast<float>(Size.X * Size.Y) / 10000.f * AreaWeight;

	for (int32 i = 0; i < Placements.Num(); i++)
	{
		for (const int32 Next : Graph.Adjacent[i])
		{
			if (Next > i && IsSizeStep(Boxes[i], Boxes[Next]))
			{
				Score += AlternateBonus;
			}
		}
	}

	// 걸어야 하는 칸이 길수록 감점. 텔레포트가 없어서 왕복이 그대로 시간이 된다
	for (const int32 Depth : Graph.Depth)
	{
		Score -= FMath::Max(0, Depth - ComfortableDepth) * LongBranchPenalty;
	}

	return Score;
}

void USectorSubsystem::openConnectedDoors()
{
	/*
	 * 조립할 때 두 문의 접점을 같은 자리에 포갰다. 그러니 접점이 겹친 문끼리가 이어진 문이다.
	 * 문 액터 피벗은 문틀 끝이라 문 폭만큼 떨어져 있어서, 피벗 거리로 찾으면 큰 문을 놓친다
	 */
	TArray<TPair<ADoor*, FVector>> Contacts;

	for (ASector* Sector : SpawnedSectors)
	{
		Sector->Init();

		for (const FSectorDoorSlot& Slot : Sector->Doors)
		{
			checkf(Slot.MeshBounds.IsValid, TEXT("[Sector] 문 메시 없음: %s"), *GetNameSafe(Slot.Door));

			Contacts.Emplace(Slot.Door, DoorContact(Slot.Door, Slot.MeshBounds).GetLocation());
		}
	}

	int32 Mated = 0;
	for (int32 i = 0; i < Contacts.Num(); i++)
	{
		for (int32 j = i + 1; j < Contacts.Num(); j++)
		{
			if (FVector::DistSquared(Contacts[i].Value, Contacts[j].Value) > Tol * Tol)
			{
				continue;
			}

			Contacts[i].Key->Open();
			Contacts[j].Key->Open();
			Mated++;
		}
	}

	// 서버는 한 번에 다 세우니 짝이 모자라면 조립이 어긋난 것이다. 클라는 조각이 하나씩 도착해서
	// 중간 상태가 정상이므로 여기서 죽이면 안 된다
	if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
	{
		return;
	}

	// 조각 N 개는 최소 N-1 군데서 문끼리 맞물려 있다. 모자라면 조립이 어긋난 것이고,
	// 그냥 두면 문이 닫힌 채로 남아 못 지나가는 맵이 나간다
	checkf(Mated >= SpawnedSectors.Num() - 1,
		   TEXT("[Sector] 맞물린 문 %d 쌍, 조각 %d 개 — 접점이 어긋났다"),
		   Mated, SpawnedSectors.Num());
}

void USectorSubsystem::ClearSpawned()
{
	for (ASector* Sector : SpawnedSectors)
	{
		// 복제 액터라 주인만 지운다. 클라 것은 서버가 지우면 같이 사라진다
		if (IsValid(Sector) && Sector->HasAuthority())
		{
			Sector->Destroy();
		}
	}

	SpawnedSectors.Reset();
	ExpectedSectorCount = 0;
}

void USectorSubsystem::RegisterReplicatedSector(ASector* Sector)
{
	SpawnedSectors.AddUnique(Sector);

	openDoorsWhenAllArrived();
}

void USectorSubsystem::openDoorsWhenAllArrived()
{
	// 하나라도 덜 왔으면 문 접점의 짝을 못 찾는다. 다 모인 뒤에 한 번만 연다
	if (ExpectedSectorCount <= 0 || SpawnedSectors.Num() < ExpectedSectorCount)
	{
		return;
	}

	ExpectedSectorCount = 0;
	openConnectedDoors();
}

FTransform USectorSubsystem::MakeEntryTransform(AActor* StartPiece)
{
	TArray<AActor*> Attached;
	StartPiece->GetAttachedActors(Attached, true, true);

	for (AActor* Actor : Attached)
	{
		if (Actor && Actor->IsA<APlayerStart>())
		{
			return Actor->GetActorTransform();
		}
	}

	/*
	 * 액터 피벗은 벽 밖이나 바닥 밑에 있을 수 있고, 거기로 TeleportTo 하면 막혀서
	 * 플레이어가 전 레벨에 그대로 남는다. 조각 한가운데 바닥 위로 보낸다
	 */
	// 화살표 기즈모와 문 자식액터까지 넣으면 중심이 방 밖으로 밀린다
	const FBox Bounds = StartPiece->GetComponentsBoundingBox(false, false);
	if (!Bounds.IsValid)
	{
		UE_LOG(LogTemp, Warning,
			   TEXT("[Sector] 입장 지점 — PlayerStart 도 바운드도 없다. 조각 피벗을 쓴다 (부착 %d개)"),
			   Attached.Num());

		return StartPiece->GetActorTransform();
	}

	const FVector Center = Bounds.GetCenter();

	UE_LOG(LogTemp, Warning,
		   TEXT("[Sector] 입장 지점 — PlayerStart 없음. 방 한가운데를 쓴다 (부착 %d개, 바닥 %.0f)"),
		   Attached.Num(), Bounds.Min.Z);

	return FTransform(StartPiece->GetActorRotation(),
					  FVector(Center.X, Center.Y, Bounds.Min.Z + EntryLift));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSectorDoorMateTest, "TeamProject.Sector.DoorMate",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSectorDoorMateTest::RunTest(const FString&)
{
	// BaseDoor_200250 을 1.3 배 조각에 달았을 때 실측값. 피벗은 문틀 한쪽 끝(Y=0)에 있다
	const FBox Mesh(FVector(-40.43f, -260.f, 0.f), FVector(14.43f, 0.f, 325.f));

	auto DoorAt = [&Mesh](const FRotator& Rot, const FVector& Pos)
	{
		const FTransform Frame(Rot, Pos);

		FSectorDoor Door;
		Door.Transform = FTransform(Frame.GetRotation(), Frame.TransformPosition(DoorAnchor(Mesh)));
		return Door;
	};

	// 두 조각의 문은 서로 다른 자리에 다른 방향으로 달려 있다
	const FSectorDoor OpenDoor = DoorAt(FRotator(0.f, -90.f, 0.f), FVector(500.f, -1000.f, 80.f));
	const FSectorDoor NewDoor = DoorAt(FRotator(0.f, 90.f, 0.f), FVector(200.f, -60.f, 81.f));

	// branchFromDoors 가 문을 월드로 올리는 식 그대로
	const FTransform Mated = NewDoor.Transform * movePos(OpenDoor, NewDoor);

	TestTrue(TEXT("두 문의 접점이 한 자리에 포개진다"),
			 Mated.GetLocation().Equals(OpenDoor.Transform.GetLocation(), 0.1f));

	TestTrue(TEXT("두 문이 마주 본다"),
			 FVector::DotProduct(Mated.GetRotation().GetForwardVector(),
								 OpenDoor.Transform.GetRotation().GetForwardVector()) < -0.99f);

	// 피벗끼리 맞대던 옛 코드는 여기서 문 폭(260)만큼 어긋났다
	const FVector HalfWidth(0.f, Mesh.GetSize().Y * 0.5f, 0.f);
	TestTrue(TEXT("문틀 옆면이 맞물린다"),
			 OpenDoor.Transform.TransformPosition(HalfWidth).Equals(
				 Mated.TransformPosition(-HalfWidth), 0.1f));

	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSectorDiagonalVolumeTest, "TeamProject.Sector.DiagonalVolume",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSectorDiagonalVolumeTest::RunTest(const FString&)
{
	// 가늘고 긴 복도를 45도로 놓으면 월드 AABB 가 990x990 으로 부푼다
	const FBox Corridor(FVector(-500.f, -200.f, 0.f), FVector(500.f, 200.f, 300.f));
	const FSectorVolume Diagonal(Corridor, FTransform(FRotator(0.f, 45.f, 0.f), FVector::ZeroVector));

	// 그 부푼 상자의 빈 모서리에 놓인 작은 방. 복도와는 닿지 않는다
	const FBox Room(FVector(-100.f, -100.f, 0.f), FVector(100.f, 100.f, 300.f));
	const FSectorVolume Corner(Room, FTransform(FVector(400.f, -400.f, 0.f)));

	TestTrue(TEXT("월드 AABB 로 누르면 겹친 것으로 보인다"),
			 DoAABB(Corridor.TransformBy(Diagonal.Frame), Room.TransformBy(Corner.Frame)));

	TestFalse(TEXT("실제로는 안 겹친다"), VolumesOverlap(Diagonal, Corner));

	const FSectorVolume Inside(Room, FTransform::Identity);
	TestTrue(TEXT("복도 한가운데 박은 방은 잡는다"), VolumesOverlap(Diagonal, Inside));

	return true;
}
#endif
