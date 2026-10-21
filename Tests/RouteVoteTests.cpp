#if WITH_DEV_AUTOMATION_TESTS

#include "GameCore/Match/RouteVoteState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRouteVoteRulesTest, "TeamProject.RouteVote.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRouteVoteRulesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	TArray<APlayerState*> Players;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Players.Add(World->SpawnActor<APlayerState>());
	}
	const FIntPoint A(0, 1), B(2, 1), C(4, 1);
	FRouteVoteState Vote;
	Vote.Round = 7;
	Vote.bActive = true;
	Vote.EndServerTime = 45.0;
	Vote.Candidates = { A, B, C };
	Vote.SetPlayers(Players);

	TestEqual(TEXT("미투표 상태에서는 조기 종료 없음"), Vote.GetWinners(false).Num(), 0);
	TestTrue(TEXT("0표 동률에는 모든 후보 포함"), Vote.GetWinners(true) == Vote.Candidates);
	TestFalse(TEXT("다른 층 선택 거부"), Vote.Submit(Players[0], 7, FIntPoint(0, 2), 0.0));
	TestFalse(TEXT("이전 투표 회차 거부"), Vote.Submit(Players[0], 6, A, 0.0));
	TestFalse(TEXT("참가하지 않은 플레이어 거부"), Vote.Submit(World->SpawnActor<APlayerState>(), 7, A, 0.0));
	TestFalse(TEXT("마감 시각에 도착한 표 거부"), Vote.Submit(Players[0], 7, A, 45.0));
	TestTrue(TEXT("첫 표 접수"), Vote.Submit(Players[0], 7, A, 1.0));
	TestFalse(TEXT("같은 곳 중복 투표 무시"), Vote.Submit(Players[0], 7, A, 1.0));
	TestEqual(TEXT("중복 클릭 후 한 표 유지"), Vote.CountVotes(A), 1);
	Vote.Submit(Players[1], 7, A, 2.0);
	Vote.Submit(Players[2], 7, A, 3.0);
	TestEqual(TEXT("과반수여도 미투표자가 있으면 대기"), Vote.GetWinners(false).Num(), 0);
	TestTrue(TEXT("마감 후에는 미투표자가 있어도 과반수 선택"), Vote.GetWinners(true) == TArray<FIntPoint>{ A });
	Vote.Submit(Players[3], 7, B, 4.0);
	TestTrue(TEXT("전원 투표와 과반수 충족 시 조기 확정"), Vote.GetWinners(false) == TArray<FIntPoint>{ A });

	Vote.Submit(Players[2], 7, B, 5.0);
	TestEqual(TEXT("변경한 표를 이전 후보에서 제거"), Vote.CountVotes(A), 2);
	TestEqual(TEXT("변경한 표를 새 후보에 추가"), Vote.CountVotes(B), 2);
	TestEqual(TEXT("2:2는 과반수가 아니므로 대기"), Vote.GetWinners(false).Num(), 0);
	TestTrue(TEXT("2:2:0에서 추첨 대상은 1위 둘뿐"), Vote.GetWinners(true) == TArray<FIntPoint>({ A, B }));
	Vote.Submit(Players[3], 7, C, 6.0);
	TestEqual(TEXT("2:1:1은 마감 전 대기"), Vote.GetWinners(false).Num(), 0);
	TestTrue(TEXT("2:1:1 마감 후 단독 최다 득표 선택"), Vote.GetWinners(true) == TArray<FIntPoint>{ A });

	Vote.SetPlayers({ Players[0], Players[1], Players[2] });
	TestEqual(TEXT("퇴장한 플레이어의 표 제거"), Vote.CountVotes(C), 0);
	TestTrue(TEXT("퇴장 후 현재 인원 기준 과반수 재평가"), Vote.GetWinners(false) == TArray<FIntPoint>{ A });
	Vote.SetPlayers(Players);
	TestEqual(TEXT("새 참가자는 미투표로 합류"), Vote.GetWinners(false).Num(), 0);
	Vote.bActive = false;
	TestFalse(TEXT("종료 후 투표 거부"), Vote.Submit(Players[3], 7, A, 7.0));
	TestEqual(TEXT("종료 후 재확정 방지"), Vote.GetWinners(true).Num(), 0);
	Vote.bActive = true;
	Vote.Votes.Reset();
	Vote.SetPlayers({ Players[0] });
	Vote.Submit(Players[0], 7, A, 8.0);
	TestTrue(TEXT("1인 플레이는 투표 즉시 확정"), Vote.GetWinners(false) == TArray<FIntPoint>{ A });
	Vote.SetPlayers({});
	TestEqual(TEXT("전원 퇴장 시 선택 없음"), Vote.GetWinners(true).Num(), 0);
	return !HasAnyErrors();
}

#endif
