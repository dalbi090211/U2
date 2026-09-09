#include "MapTransObject.h"

#include "Components/StaticMeshComponent.h"
#include "GameCore/BasePlayerController.h"
#include "GameCore/Interact/InteractTriggerComponent.h"
#include "MapBuild/WorldGenSubSystem.h"

AMapTransObject::AMapTransObject()
{
	// 포탈 메시. 에셋은 BP에서 지정, 충돌 없음
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 베이스는 Trigger를 루트로 두지만 포탈은 메시가 루트다. 배치된 BP의 계층과 어긋나면 로드가 중단된다
	RootComponent = Mesh;
	Trigger->SetupAttachment(Mesh);

	InteractText = NSLOCTEXT("Interact", "EnterPortal", "포탈 진입");

	// 진입 연출 기본값. 개별 포탈은 BP나 디테일 패널에서 조정한다
	FadeDuration = 1.f;
	HoldTime = 1.f;
}

void AMapTransObject::CompleteInteract()
{
	Super::CompleteInteract();	//PreEvents 발화
	OpenRouteMap();
}

void AMapTransObject::OpenRouteMap()
{
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
			BPC->Client_OpenRouteMap(Seed, CurPos, Sys->GetVisited());
		}
	}
}
