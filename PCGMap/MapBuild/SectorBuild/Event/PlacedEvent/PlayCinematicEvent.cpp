#include "PlayCinematicEvent.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameCore/Cinematic/CinematicStandInComponent.h"
#include "GameCore/BasePlayerController.h"
#include "PCGMap/MapBuild/SectorBuild/Event/EventManager.h"

namespace
{
	// 컷신은 한 번에 하나만 돈다. 스킵 요청이 어느 컷신을 가리키는지 따로 실어 보낼 필요가 없다
	TWeakObjectPtr<APlayCinematicEvent> GPlayingCinematic;
}

APlayCinematicEvent::APlayCinematicEvent()
{
	PrimaryActorTick.bCanEverTick = false;

	// 멀티캐스트로 전원에게 재생을 알린다
	bReplicates = true;
}

APlayCinematicEvent* APlayCinematicEvent::GetPlaying()
{
	return GPlayingCinematic.Get();
}

void APlayCinematicEvent::Execute(const UObject* WorldContext)
{
	Super::Execute(WorldContext);

	if (!HasAuthority() || bFinished)
	{
		return;
	}

	// 시퀀스를 안 꽂았으면 컷신 없이 뒤만 잇는다. 여기서 죽이면 맵이 통째로 안 돈다
	if (!Sequence)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cinematic] 시퀀스 없음: %s"), *GetName());
		FinishOnServer();
		return;
	}

	SkipVotes.Reset();
	MulticastPlay();
	MulticastSkipVotes(0, CountPlayers());
}

void APlayCinematicEvent::MulticastPlay_Implementation()
{
	GPlayingCinematic = this;
	SetCinematicInput(true);

	if (!Sequence)
	{
		return;
	}

	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bDisableLookAtInput = true;
	Settings.bDisableMovementInput = true;

	// 대역을 쓰면 끝난 자리를 읽어 플레이어를 옮겨야 한다. 멈출 때 원위치로 되돌리지 않는다
	if (UsesStandIns())
	{
		Settings.FinishCompletionStateOverride = EMovieSceneCompletionModeOverride::ForceKeepState;
		BeginStandIns();
	}

	// 배치된 카메라와 대역의 참조를 현재 스트리밍 레벨 인스턴스에서 해석한다.
	FActorSpawnParameters SpawnParams;
	SpawnParams.OverrideLevel = GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bAllowDuringConstructionScript = true;
	SpawnParams.bDeferConstruction = true;

	ALevelSequenceActor* SpawnedActor = GetWorld()->SpawnActor<ALevelSequenceActor>(SpawnParams);
	if (!SpawnedActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cinematic] 시퀀스 액터 생성 실패: %s"), *GetName());
		return;
	}

	SequenceActor = SpawnedActor;
	SequencePlayer = SpawnedActor->GetSequencePlayer();

	if (!SequencePlayer)
	{
		return;
	}

	SequenceActor->PlaybackSettings = Settings;
	SequencePlayer->SetPlaybackSettings(Settings);
	SequenceActor->SetSequence(Sequence);
	SequenceActor->InitializePlayer();
	SequenceActor->FinishSpawning(FTransform::Identity);

	// 서버의 시퀀스가 끝나는 시점이 기준이다. 머신마다 따로 끝내면 보스가 누구 화면에서는 먼저 움직인다
	if (HasAuthority())
	{
		SequencePlayer->OnFinished.AddDynamic(this, &APlayCinematicEvent::OnSequenceFinished);
	}

	SequencePlayer->Play();
}

void APlayCinematicEvent::OnSequenceFinished()
{
	FinishOnServer();
}

void APlayCinematicEvent::RequestSkip(APlayerController* Voter)
{
	if (!HasAuthority() || bFinished || !IsValid(Voter))
	{
		return;
	}

	SkipVotes.Add(Voter);
	MulticastSkipVotes(SkipVotes.Num(), CountPlayers());

	// 전원이 눌러야 넘어간다
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (IsValid(PC) && !SkipVotes.Contains(PC))
		{
			return;
		}
	}

	FinishOnServer();
}

void APlayCinematicEvent::FinishOnServer()
{
	if (!HasAuthority() || bFinished)
	{
		return;
	}

	bFinished = true;

	MulticastStop();

	// 블렌드가 필요 없으면 바로 뒤를 잇는다
	if (!ShouldBlendReturn())
	{
		CompleteReturn();
		return;
	}

	// 전원의 카메라가 자기 자리로 돌아온 뒤에 뒤를 잇는다. 응답 없는 플레이어는 타임아웃이 대신한다.
	// 폰이 자리에 도착하기를 기다리는 시간도 그 안에 들어간다
	ReturnReady.Reset();
	GetWorld()->GetTimerManager().SetTimer(
		ReturnTimeoutTimer, this, &APlayCinematicEvent::CompleteReturn,
		ReturnArrivalMaxWait + ReturnBlendTime + ReturnReadyTimeout, false);
}

void APlayCinematicEvent::NotifyReturnReady(APlayerController* Controller)
{
	if (!HasAuthority() || !bFinished || bReturnCompleted || !IsValid(Controller))
	{
		return;
	}

	ReturnReady.Add(Controller);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (IsValid(PC) && !ReturnReady.Contains(PC))
		{
			return;
		}
	}

	CompleteReturn();
}

void APlayCinematicEvent::CompleteReturn()
{
	if (!HasAuthority() || bReturnCompleted)
	{
		return;
	}

	bReturnCompleted = true;
	GetWorld()->GetTimerManager().ClearTimer(ReturnTimeoutTimer);

	// 블렌드를 기다렸다면 조작은 여기서 전원에게 함께 돌려준다. 카메라가 끝나는 순간과 전투가 열리는 순간을 맞춘다
	if (ShouldBlendReturn())
	{
		MulticastReturnFinished();
	}

	RunEventGroups(PostEvents);
}

void APlayCinematicEvent::MulticastReturnFinished_Implementation()
{
	EndCinematicLocal();
}

void APlayCinematicEvent::EndCinematicLocal()
{
	// 타임아웃으로 먼저 끝났다면 도착 대기나 블렌드 타이머가 남아 있을 수 있다
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReturnArrivalTimer);
		World->GetTimerManager().ClearTimer(ReturnBlendTimer);
	}

	if (IsValid(ReturnCamera))
	{
		ReturnCamera->Destroy();
	}
	ReturnCamera = nullptr;

	SetCinematicInput(false);
	SetCinematicSkipVotesOnHud(0, 0);

	if (GPlayingCinematic == this)
	{
		GPlayingCinematic = nullptr;
	}
}

APlayerController* APlayCinematicEvent::GetLocalController() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetFirstLocalPlayerController(GetWorld()) : nullptr;
}

void APlayCinematicEvent::BeginCameraReturn(APlayerController* Controller, const FMinimalViewInfo& StartView)
{
	if (!IsValid(Controller))
	{
		return;
	}

	// 시퀀스가 멈추면 시점이 원래 타깃으로 튄다. 멈추기 직전 화면을 임시 카메라로 붙잡아 둔다.
	// 폰이 슬롯 자리에 도착할 때까지 이 화면을 유지하고, 도착한 뒤에 여기서 블렌드를 출발시킨다
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ReturnCamera = GetWorld()->SpawnActor<ACameraActor>(StartView.Location, StartView.Rotation, SpawnParams);
	if (IsValid(ReturnCamera))
	{
		if (UCameraComponent* Camera = ReturnCamera->GetCameraComponent())
		{
			Camera->SetFieldOfView(StartView.FOV);
			Camera->SetConstraintAspectRatio(StartView.bConstrainAspectRatio);
			Camera->SetAspectRatio(StartView.AspectRatio);
		}
		Controller->SetViewTarget(ReturnCamera);
	}

	ReturnArrivalStartTime = GetWorld()->GetTimeSeconds();

	// 서버 자신은 이미 자리에 있다. 이미 모두 도착했다면 기다리지 않는다
	if (HavePlayersArrived())
	{
		StartCameraBlend();
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		ReturnArrivalTimer, this, &APlayCinematicEvent::PollPlayersArrived, 0.05f, true);
}

bool APlayCinematicEvent::HavePlayersArrived() const
{
	const TArray<APawn*> Pawns = GetPawnsInSlotOrder();

	for (int32 Slot = 0; Slot < Pawns.Num(); ++Slot)
	{
		const FTransform* Stand = ReturnSlotTransforms.Find(Slot);
		if (!Stand || !IsValid(Pawns[Slot]))
		{
			continue;
		}

		// 대역 피벗은 발, 캐릭터 피벗은 캡슐 가운데다. 서버가 폰을 옮길 때와 같은 기준으로 예상 위치를 잡는다
		const ACharacter* Character = Cast<ACharacter>(Pawns[Slot]);
		const float HalfHeight = Character && Character->GetCapsuleComponent()
			? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 0.0f;

		const FVector Expected = Stand->GetLocation() + FVector(0.0, 0.0, HalfHeight);
		if (FVector::Dist(Pawns[Slot]->GetActorLocation(), Expected) > ReturnArrivalTolerance)
		{
			return false;
		}
	}

	return true;
}

void APlayCinematicEvent::PollPlayersArrived()
{
	const bool bTimedOut = GetWorld()->GetTimeSeconds() - ReturnArrivalStartTime >= ReturnArrivalMaxWait;
	if (!bTimedOut && !HavePlayersArrived())
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ReturnArrivalTimer);
	StartCameraBlend();
}

void APlayCinematicEvent::StartCameraBlend()
{
	// 폰이 자리에 선 뒤에야 대역을 치우고 실제 플레이어를 보인다. 그래야 옛 자리에서 튀는 폰이 화면에 안 나온다
	SwapToPlayers();

	APlayerController* Controller = GetLocalController();
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		// 블렌드해 갈 폰이 없다. 기다리게 하지 말고 바로 끝났다고 알린다
		FinishCameraReturn();
		return;
	}

	Controller->SetViewTargetWithBlend(Pawn, ReturnBlendTime, VTBlend_Cubic, 2.0f, true);

	// 블렌드가 정확히 끝나는 프레임과 겹치지 않게 조금 여유를 둔다
	GetWorld()->GetTimerManager().SetTimer(
		ReturnBlendTimer, this, &APlayCinematicEvent::FinishCameraReturn, ReturnBlendTime + 0.1f, false);
}

void APlayCinematicEvent::FinishCameraReturn()
{
	GetWorld()->GetTimerManager().ClearTimer(ReturnBlendTimer);

	if (IsValid(ReturnCamera))
	{
		ReturnCamera->Destroy();
	}
	ReturnCamera = nullptr;

	// 판정은 서버가 한다. 이 머신은 제 카메라가 자리에 도착했다고만 알린다
	if (ABasePlayerController* Controller = Cast<ABasePlayerController>(GetLocalController()))
	{
		Controller->Server_NotifyCinematicReturnReady();
	}
}

void APlayCinematicEvent::MulticastStop_Implementation()
{
	const bool bBlendReturn = ShouldBlendReturn();

	// 정지와 대역 정리가 시점을 바꾸기 전에, 지금 이 플레이어가 보고 있는 화면을 잡아 둔다.
	// 자연 종료면 마지막 장면이고, 스킵이면 그 순간의 장면이다
	APlayerController* LocalController = bBlendReturn ? GetLocalController() : nullptr;
	FMinimalViewInfo StartView;
	if (LocalController && LocalController->PlayerCameraManager)
	{
		StartView = LocalController->PlayerCameraManager->GetCameraCacheView();
	}

	if (UsesStandIns())
	{
		// 스킵이면 도중에 멈춘다. 대역을 끝 장면으로 보내 둬야 모두가 도착 지점에 선다.
		// 끝 프레임은 범위 밖(배타)이라 한 프레임 앞을 평가한다
		if (SequencePlayer)
		{
			SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(
				SequencePlayer->GetEndTime().Time - FFrameTime(1), EUpdatePositionMethod::Jump));
		}

		// 정지가 트랙 상태를 되돌리기 전에 대역 자리를 읽는다.
		// 블렌드를 기다리는 경우 대역을 치우고 폰을 보이는 일은 폰이 자리에 도착한 뒤로 미룬다
		if (bBlendReturn)
		{
			PlaceAtStandIns();
		}
		else
		{
			EndStandIns();
		}
	}

	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer = nullptr;
	}

	if (IsValid(SequenceActor))
	{
		SequenceActor->Destroy();
		SequenceActor = nullptr;
	}

	if (bBlendReturn)
	{
		// 조작과 컷신 상태는 전원의 블렌드가 끝난 뒤 MulticastReturnFinished 가 돌려준다.
		// GPlayingCinematic 도 그때까지 둬야 카메라가 도착했다는 보고가 이 컷신에 닿는다
		SetCinematicSkipVotesOnHud(0, 0);

		// 화면이 없는 머신(전용 서버)은 기다릴 것이 없다. 바로 플레이어를 원래 상태로 돌린다
		if (!IsValid(LocalController))
		{
			SwapToPlayers();
			return;
		}

		BeginCameraReturn(LocalController, StartView);
		return;
	}

	EndCinematicLocal();
}

void APlayCinematicEvent::SetCinematicInput(bool bOn)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();

		// 화면과 입력은 각 머신이 제 것만 만진다
		if (!IsValid(PC) || !PC->IsLocalController())
		{
			continue;
		}

		// 대역을 쓰면 전원 숨김은 SetPlayersHidden 이 맡는다. 여기서 또 숨기면 끌 때 서로 덮어쓴다
		PC->SetCinematicMode(bOn, bHidePlayer && !UsesStandIns(), true, true, true);

		/*
		 * SetCinematicMode 는 이동과 시점만 막는다. 사격처럼 폰에 바인딩된 액션은 그대로 들어와서
		 * 컷신 중에 총이 나간다. 폰 입력을 통째로 끈다 — ESC 는 컨트롤러에 걸려 있어 살아남는다
		 */
		if (ABasePlayerController* BPC = Cast<ABasePlayerController>(PC))
		{
			BPC->SetCinematicUI(bOn);
		}

		if (APawn* Pawn = PC->GetPawn())
		{
			if (bOn)
			{
				Pawn->DisableInput(PC);
			}
			else
			{
				Pawn->EnableInput(PC);
			}
		}
	}
}

int32 APlayCinematicEvent::CountPlayers() const
{
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (IsValid(It->Get()))
		{
			Count++;
		}
	}
	return Count;
}

void APlayCinematicEvent::MulticastSkipVotes_Implementation(int32 Voted, int32 Total)
{
	SetCinematicSkipVotesOnHud(Voted, Total);
}

void APlayCinematicEvent::SetCinematicSkipVotesOnHud(int32 Voted, int32 Total) const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UEventManager* Events = GI->GetSubsystem<UEventManager>())
		{
			Events->SetCinematicSkipVotes(Voted, Total);
		}
	}
}

TArray<APawn*> APlayCinematicEvent::GetPawnsInSlotOrder() const
{
	TArray<APlayerState*> PlayerStates;
	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			if (IsValid(PlayerState) && IsValid(PlayerState->GetPawn()))
			{
				PlayerStates.Add(PlayerState);
			}
		}
	}

	// PlayerArray 순서는 머신마다 다를 수 있다. 서버가 정해 복제한 PlayerId 로 맞춘다
	PlayerStates.Sort([](const APlayerState& A, const APlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });

	TArray<APawn*> Pawns;
	for (const APlayerState* PlayerState : PlayerStates)
	{
		Pawns.Add(PlayerState->GetPawn());
	}
	return Pawns;
}

void APlayCinematicEvent::BeginStandIns()
{
	const TArray<APawn*> Pawns = GetPawnsInSlotOrder();

	for (int32 Slot = 0; Slot < StandInSlots.Num(); ++Slot)
	{
		UCinematicStandInComponent* StandIn = IsValid(StandInSlots[Slot])
			? StandInSlots[Slot]->FindComponentByClass<UCinematicStandInComponent>()
			: nullptr;
		if (!StandIn)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Cinematic] 대역 슬롯 %d 에 CinematicStandInComponent 가 없다: %s"),
				Slot, *GetNameSafe(StandInSlots[Slot]));
			continue;
		}

		// 인원보다 뒤 슬롯은 비운다
		if (Pawns.IsValidIndex(Slot))
		{
			StandIn->ApplyAppearance(Pawns[Slot]);
		}
		else
		{
			StandIn->ClearAppearance();
		}
	}

	if (Pawns.Num() > StandInSlots.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cinematic] 대역 슬롯(%d)보다 플레이어(%d)가 많다: %s"),
			StandInSlots.Num(), Pawns.Num(), *GetName());
	}

	SetPlayersHidden(true);
}

void APlayCinematicEvent::EndStandIns()
{
	PlaceAtStandIns();
	SwapToPlayers();
}

void APlayCinematicEvent::PlaceAtStandIns()
{
	const TArray<APawn*> Pawns = GetPawnsInSlotOrder();

	ReturnSlotTransforms.Reset();

	for (int32 Slot = 0; Slot < StandInSlots.Num(); ++Slot)
	{
		UCinematicStandInComponent* StandIn = IsValid(StandInSlots[Slot])
			? StandInSlots[Slot]->FindComponentByClass<UCinematicStandInComponent>()
			: nullptr;
		if (!StandIn)
		{
			continue;
		}

		// 폰이 이 자리에 도착했는지 판정할 기준. 시퀀스가 멈추기 전, 대역이 끝 장면에 서 있을 때 읽어 둔다
		ReturnSlotTransforms.Add(Slot, StandIn->GetStandTransform());

		// 위치는 서버가 옮기고 복제로 퍼진다. 대역이 끝 장면에 서 있을 때 읽어야 한다
		if (HasAuthority() && Pawns.IsValidIndex(Slot))
		{
			APawn* Pawn = Pawns[Slot];
			const FTransform Stand = StandIn->GetStandTransform();

			// 대역 피벗은 발, 캐릭터 피벗은 캡슐 가운데다
			const ACharacter* Character = Cast<ACharacter>(Pawn);
			const float HalfHeight = Character && Character->GetCapsuleComponent()
				? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
				: 0.0f;

			Pawn->TeleportTo(Stand.GetLocation() + FVector(0.0, 0.0, HalfHeight), Stand.Rotator());

			// 시점은 각 클라가 쥐고 있다. 서버에서 돌리면 다음 입력에 덮인다
			if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
			{
				PC->ClientSetRotation(Stand.Rotator());
			}
		}

		// 카메라가 자기 자리로 블렌드해 도착했을 때 보스 쪽을 보고 있도록, 이 머신의 플레이어 시점을 블렌드 전에 직접 맞춘다.
		// 위의 ClientSetRotation 은 네트워크를 거쳐 블렌드 도중에 도착할 수 있다
		if (Pawns.IsValidIndex(Slot))
		{
			APlayerController* LocalPC = Cast<APlayerController>(Pawns[Slot]->GetController());
			if (LocalPC && LocalPC->IsLocalController())
			{
				LocalPC->SetControlRotation(StandIn->GetStandTransform().Rotator());
			}
		}
	}
}

void APlayCinematicEvent::SwapToPlayers()
{
	for (AActor* Slot : StandInSlots)
	{
		UCinematicStandInComponent* StandIn = IsValid(Slot)
			? Slot->FindComponentByClass<UCinematicStandInComponent>()
			: nullptr;
		if (StandIn)
		{
			StandIn->ClearAppearance();
		}
	}

	SetPlayersHidden(false);
}

void APlayCinematicEvent::SetPlayersHidden(const bool bHide)
{
	if (!bHide)
	{
		for (const TPair<TWeakObjectPtr<AActor>, bool>& Pair : HiddenBeforeCinematic)
		{
			if (AActor* Actor = Pair.Key.Get())
			{
				Actor->SetActorHiddenInGame(Pair.Value);
			}
		}
		HiddenBeforeCinematic.Reset();
		return;
	}

	// 무기는 따로 붙은 액터라 폰을 숨겨도 남는다. 붙은 액터까지 함께 숨긴다
	for (APawn* Pawn : GetPawnsInSlotOrder())
	{
		TArray<AActor*> Targets;
		Targets.Add(Pawn);
		Pawn->GetAttachedActors(Targets, false, true);

		for (AActor* Actor : Targets)
		{
			if (IsValid(Actor) && !HiddenBeforeCinematic.Contains(Actor))
			{
				HiddenBeforeCinematic.Add(Actor, Actor->IsHidden());
				Actor->SetActorHiddenInGame(true);
			}
		}
	}
}


