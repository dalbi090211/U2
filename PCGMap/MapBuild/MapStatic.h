#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "GameCore/BasePlayerController.h"

// 나가는 페이드 시간. 서버는 이만큼 기다렸다 넘어가고, 클라는 이만큼 어두워진다
inline constexpr float LevelFadeOutTime = 0.6f;

/*
 * 레벨 이름 하나로 심리스 트래블. 서버 권한에서만 실제로 넘어간다.
 *
 * 레벨 전환은 전부 이 함수를 거친다. 화면을 먼저 어둡게 깔고, 다 내려간 뒤에 넘어간다.
 * ServerTravel 을 직접 부르면 페이드가 보이기 전에 월드가 갈린다.
 *
 * APlayerController 에 SeamlessTravelTo 멤버가 있어서 이름을 겹치면 안 된다.
 */
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

//영역 충돌 체크
static bool DoAABB(const FBox& AA, const FBox& BB)
{
	constexpr float Tol = 10.f;
	return AA.ExpandBy(-Tol).Intersect(BB.ExpandBy(-Tol));
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

inline FRandomStream randStream;