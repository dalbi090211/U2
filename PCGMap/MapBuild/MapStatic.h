#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "GameCore/BasePlayerController.h"

// 나가는 페이드 시간. 서버는 이만큼 기다렸다 넘어가고, 클라는 이만큼 어두워진다
inline constexpr float LevelFadeOutTime = 0.6f;
inline constexpr float Tol = 10.f;
inline FRandomStream randStream;

inline bool StartLevelTransition(const UObject* WorldContextObject, FName LevelName)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || LevelName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Travel] 월드가 없거나 레벨 이름이 비었다."));
		return false;
	}

	AGameModeBase* GameMode = World->GetAuthGameMode();
	if (!GameMode)
	{
		// 클라에서 부르면 아무 일도 안 일어난다. 서버로 RPC를 보낸 뒤에 부를 것.
		UE_LOG(LogTemp, Warning, TEXT("[Travel] 서버 권한이 아니다. %s 이동 무시."), *LevelName.ToString());
		return false;
	}

	// 심리스여야 PlayerController/PlayerState가 CopyProperties로 넘어간다.
	GameMode->bUseSeamlessTravel = true;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController* BPC = Cast<ABasePlayerController>(It->Get()))
		{
			BPC->Client_BeginLevelTransition(LevelFadeOutTime);
		}
	}

	const FString URL = LevelName.ToString();
	TWeakObjectPtr<UWorld> WeakWorld = World;

	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(
		Handle,
		FTimerDelegate::CreateLambda([WeakWorld, URL]()
		{
			if (UWorld* TravelWorld = WeakWorld.Get())
			{
				TravelWorld->ServerTravel(URL, /*bAbsolute=*/false);
			}
		}),
		LevelFadeOutTime,
		false);

	return true;
}

/*
 * 영역 충돌 체크. Tol 이하로 파고든 것은 겹침으로 안 본다 — 이음매에서 맞닿는 면 때문에.
 * ExpandBy(-Tol) 로 양쪽을 줄이면 Tol*2 보다 얇은 볼륨이 뒤집혀 판정이 통째로 꺼진다
 */
static bool DoAABB(const FBox& AA, const FBox& BB)
{
	if (!AA.Intersect(BB))
	{
		return false;
	}

	return AA.Overlap(BB).GetSize().GetMin() > Tol;
}

// 파일 로컬 헬퍼
template <typename T>
static void LoadClassArray(const TArray<TSoftClassPtr<T>>& In,
						   TArray<TSubclassOf<T>>& Out,
						   const TCHAR* Label)
{
	Out.Reset();
	Out.Reserve(In.Num());

	for (const TSoftClassPtr<T>& Soft : In)
	{
		if (Soft.IsNull()) { continue; }

		if (UClass* Loaded = Soft.LoadSynchronous())
		{
			Out.Add(Loaded);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("%s 로드 실패: %s"), Label, *Soft.ToString());
		}
	}
}