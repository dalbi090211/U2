#include "BaseGameMode.h"

#include "BasePlayerController.h"
#include "BasePlayerState.h"
#include "BaseGameState.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameCore/Attribute/CharacterAttributeSet.h"
#include "GameCore/Character/BaseCharacter.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "GameCore/Match/PartyRoomSubsystem.h"
#include "PCGMap/MapBuild/MapStatic.h"
#include "PCGMap/MapBuild/WorldGenSubSystem.h"

ABaseGameMode::ABaseGameMode()
{
	//레벨 간 정보가 다르면 재생성, 같으면 데이터만 복사후 재생성. 포인터가 유지되지 않음
	PlayerControllerClass = ABasePlayerController::StaticClass();
	PlayerStateClass = ABasePlayerState::StaticClass();
	GameStateClass = ABaseGameState::StaticClass();

	// 트래블 시 참가자 접속과 PlayerState 유지
	bUseSeamlessTravel = true;
}

void ABaseGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	// Login 단계에서 채워진 이름을 그대로 쓴다 (Steam이면 페르소나 이름)
	if (ABasePlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ABasePlayerState>() : nullptr)
	{
		PS->SetPlayerNickname(PS->GetPlayerName());
		if (GameState && PS->GetRoomJoinOrder() == INDEX_NONE)
		{
			int32 NextOrder = 0;
			for (const APlayerState* Existing : GameState->PlayerArray)
			{
				if (const ABasePlayerState* ExistingPS = Cast<ABasePlayerState>(Existing))
				{
					NextOrder = FMath::Max(NextOrder, ExistingPS->GetRoomJoinOrder() + 1);
				}
			}
			PS->SetRoomJoinOrder(NextOrder);
		}
	}
	if (ABaseGameState* GS = GetGameState<ABaseGameState>())
	{
		GS->HandleRoutePlayerJoined(NewPlayer);
	}
}

void ABaseGameMode::KillAllSurvivors()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ABaseCharacter* Character = It->IsValid() ? Cast<ABaseCharacter>(It->Get()->GetPawn()) : nullptr;
		UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;

		// 이미 죽은 캐릭터는 건너뛴다
		if (!ASC || ASC->HasMatchingGameplayTag(GameplayTagSystem::State_Dead))
		{
			continue;
		}

		ASC->ApplyModToAttribute(UCharacterAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0.0f);

		// 실제 사망과 같은 경로. GA_Death가 어빌리티 취소와 State_Dead를 처리한다
		FGameplayEventData Payload;
		Payload.EventTag = GameplayTagSystem::Event_Death;
		Payload.Target = Character;
		ASC->HandleGameplayEvent(GameplayTagSystem::Event_Death, &Payload);
	}
}

void ABaseGameMode::Logout(AController* Exiting)
{
	if (ABaseGameState* GS = GetGameState<ABaseGameState>())
	{
		GS->HandlePlayerLeaving(Exiting ? Exiting->PlayerState.Get() : nullptr);
	}
	Super::Logout(Exiting);
}

bool ABaseGameMode::ReturnPartyToRoom()
{
	UPartyRoomSubsystem* Room = GetGameInstance()->GetSubsystem<UPartyRoomSubsystem>();
	const FName Destination = Room && !Room->GetRoomMap().IsNone() ? Room->GetRoomMap() : DefaultReturnRoomMap;
	if (!HasAuthority() || Destination.IsNone())
	{
		UE_LOG(LogTemp, Error, TEXT("[PartyReturn] 대기방 복귀 목적지가 없습니다."));
		return false;
	}
	// 이동 예약이 성공한 뒤에만 런을 정리한다. 이 함수는 GS에서 한 번만 호출한다.
	if (!StartLevelTransition(this, Destination))
	{
		return false;
	}
	if (Room)
	{
		Room->MarkReturning();
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController* PC = Cast<ABasePlayerController>(It->Get()))
		{
			PC->Client_PreparePartyReturn();
			if (ABasePlayerState* PS = PC->GetPlayerState<ABasePlayerState>())
			{
				PS->ResetRunData();
			}
		}
	}
	if (UWorldGenSubSystem* WorldGen = GetGameInstance()->GetSubsystem<UWorldGenSubSystem>())
	{
		WorldGen->AbandonRun();
	}
	UE_LOG(LogTemp, Log, TEXT("[PartyReturn] 전원 준비 완료. 대기방 이동: %s"), *Destination.ToString());
	return true;
}
