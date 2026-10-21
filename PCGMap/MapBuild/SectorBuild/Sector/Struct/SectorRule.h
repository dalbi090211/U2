#pragma once

#include "CoreMinimal.h"
#include "PCGMap/MapBuild/SectorBuild/Sector/Struct/SectorPieceType.h"

/*
 * 자리 하나를 채우기 직전, 타입별 확률에 끼어드는 룰. Ctx 에 등록된 것만 돌아간다.
 * Weights 는 ESectorPieceType 으로 색인한다 — 0 으로 만들면 그 타입은 이 자리에서 빠진다.
 * Remaining 은 이 자리를 포함해 앞으로 채울 자리 수다.
 * PrevType 이 비어 있으면 첫 자리, 즉 플레이어가 지나온 방이 아직 없다는 뜻이다
 */
class FSectorRule
{
public:
	virtual ~FSectorRule() = default;

	virtual void Execute(int32 Remaining, const TOptional<ESectorPieceType>& PrevType,
						 TArray<float>& Weights) = 0;
};

// 첫 자리는 Start 만 쓴다. 기본 확률에서 Start 가 0 이라 이 룰이 올려 주지 않으면 안 나온다
class FFirstSectorRule : public FSectorRule
{
public:
	virtual void Execute(int32 Remaining, const TOptional<ESectorPieceType>& PrevType,
						 TArray<float>& Weights) override
	{
		if (PrevType.IsSet())
		{
			return;
		}

		for (float& Weight : Weights)
		{
			Weight = 0.f;
		}
		Weights[(int32)ESectorPieceType::Start] = 1.f;
	}
};

// 전투방이 연달아 나오면 플레이어에겐 같은 방을 두 번 지난 것으로 보인다
class FBanContInterRule : public FSectorRule
{
public:
	virtual void Execute(int32 Remaining, const TOptional<ESectorPieceType>& PrevType,
						 TArray<float>& Weights) override
	{
		if (PrevType.IsSet() && PrevType.GetValue() == ESectorPieceType::Battle)
		{
			Weights[(int32)ESectorPieceType::Battle] = 0.f;
		}
	}
};

// 접합부도 마찬가지. 통로만 두 번 이어지면 방 사이가 지루하게 길어진다
class FBanContConnectRule : public FSectorRule
{
public:
	virtual void Execute(int32 Remaining, const TOptional<ESectorPieceType>& PrevType,
						 TArray<float>& Weights) override
	{
		if (PrevType.IsSet() && PrevType.GetValue() == ESectorPieceType::Joint)
		{
			Weights[(int32)ESectorPieceType::Joint] = 0.f;
		}
	}
};
