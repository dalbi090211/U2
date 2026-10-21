// Fill out your copyright notice in the Description page of Project Settings.


#include "GameCore/AI/BaseAIController.h"

#include "Components/StateTreeAIComponent.h"

ABaseAIController::ABaseAIController()
{
	StateTreeAIComponent = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAIComponent"));

	BrainComponent = StateTreeAIComponent;

	/*
	 * 컨트롤러 BeginPlay 에서 자동 시작하면 풀링이 폰보다 먼저 컨트롤러를 만드는 시점이라 Pawn 컨텍스트를 못 찾아 실패한다.
	 * 시작은 폰을 잡은 뒤 OnPossess 가 맡는다.
	 */
	StateTreeAIComponent->SetStartLogicAutomatically(false);
}

void ABaseAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (StateTreeAIComponent && !StateTreeAIComponent->IsRunning())
	{
		StateTreeAIComponent->StartLogic();
	}
}
