#include "MapTransObject.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameCore/BasePlayerController.h"
#include "GameCore/BaseGameState.h"
#include "GameCore/Interact/InteractTriggerComponent.h"
#include "GameCore/Tags/GameplayTagSystem.h"
#include "MapBuild/WorldGenSubSystem.h"

AMapTransObject::AMapTransObject()
{
	// 포탈 메시. 에셋은 BP에서 지정, 충돌 없음
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 베이스는 Trigger를 루트로 두지만 포탈은 메시가 루트다. 배치된 BP의 계층과 어긋나면 로드가 중단된다
	RootComponent = Mesh;
	Trigger->SetupAttachment(Mesh);

	Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("Audio"));
	Audio->SetupAttachment(Mesh);
	Audio->bAutoActivate = false;

	InteractText = NSLOCTEXT("Interact", "EnterPortal", "포탈 진입");

	// 진입 연출 기본값. 개별 포탈은 BP나 디테일 패널에서 조정한다
	FadeDuration = 1.f;
	HoldTime = 1.f;
}

void AMapTransObject::BeginPlay()
{
	Super::BeginPlay();

	// 숨은 채 시작한 포탈은 열릴 때 OnRep_Active 가 튼다
	if (IsInteractActive())
	{
		PlayExclusive(AmbientSound);
	}
}

void AMapTransObject::OnRep_Active()
{
	Super::OnRep_Active();
	PlayExclusive(AmbientSound);
}

void AMapTransObject::OnRep_Triggered()
{
	Super::OnRep_Triggered();
	PlayExclusive(InteractSound);
}

void AMapTransObject::MulticastRouteMapOpened_Implementation()
{
	PlayExclusive(RouteMapOpenSound);
}

void AMapTransObject::PlayExclusive(USoundBase* Sound)
{
	if (!Sound)
	{
		return;
	}

	// SetSound 는 재생 중이면 새 소리를 스스로 틀어서, 멈추지 않고 Play 하면 두 번 시작된다
	Audio->Stop();
	Audio->SetSound(Sound);
	Audio->Play();
}

void AMapTransObject::CompleteInteract()
{
	Super::CompleteInteract();	//PreEvents 발화
	OpenRouteMap();
}

void AMapTransObject::OpenRouteMap()
{
	ABaseGameState* GS = GetWorld()->GetGameState<ABaseGameState>();
	check(GS);
	if (!GS->StartRouteVote())
	{
		return;
	}
	UWorldGenSubSystem* Sys = GetWorld()->GetGameInstance()->GetSubsystem<UWorldGenSubSystem>();
	if (!Sys)
	{
		return;
	}

	// 실제로 여는 것은 각 플레이어의 머신
	const int32 Seed = Sys->GetRunSeed();
	const FIntPoint CurPos = Sys->GetCurPos();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ABasePlayerController* BPC = Cast<ABasePlayerController>(It->Get()))
		{
			if (UAbilitySystemComponent* ASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(BPC->GetPawn()))
			{
				// GA_Interact 로 받아둔 무적을 루트맵 보는 동안까지 이어받는다. 레벨이 바뀌며 ASC와 함께 사라진다
				ASC->AddLooseGameplayTag(GameplayTagSystem::State_Invincible);
			}

			BPC->Client_OpenRouteMap(Seed, Sys->GetSheet(), CurPos, Sys->GetVisited(),
				GS->GetRouteVote().Round, GS->GetRouteVote().EndServerTime);
		}
	}

	MulticastRouteMapOpened();
}
