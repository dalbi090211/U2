#pragma once

#include "CoreMinimal.h"
#include "RouteVoteState.generated.h"

class APlayerState;

USTRUCT()
struct FRoutePlayerVote
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<APlayerState> Player;

	UPROPERTY()
	FIntPoint Node = FIntPoint(INDEX_NONE, INDEX_NONE);
};

USTRUCT()
struct FRouteVoteState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Round = 0;

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	double EndServerTime = 0.0;

	UPROPERTY()
	TArray<FIntPoint> Candidates;

	UPROPERTY()
	TArray<FRoutePlayerVote> Votes;

	void SetPlayers(const TArray<APlayerState*>& Players);
	bool Submit(APlayerState* Player, int32 InRound, FIntPoint Node, double ServerTime);
	int32 CountVotes(FIntPoint Node) const;
	TArray<FIntPoint> GetWinners(bool bTimeExpired) const;
};
