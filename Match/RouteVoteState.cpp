#include "RouteVoteState.h"

#include "GameFramework/PlayerState.h"

void FRouteVoteState::SetPlayers(const TArray<APlayerState*>& Players)
{
	Votes.RemoveAll([&Players](const FRoutePlayerVote& Vote) { return !Players.Contains(Vote.Player); });
	for (APlayerState* Player : Players)
	{
		if (!Votes.ContainsByPredicate([Player](const FRoutePlayerVote& Vote) { return Vote.Player == Player; }))
		{
			FRoutePlayerVote& Vote = Votes.AddDefaulted_GetRef();
			Vote.Player = Player;
		}
	}
}

bool FRouteVoteState::Submit(APlayerState* Player, int32 InRound, FIntPoint Node, double ServerTime)
{
	if (!bActive || InRound != Round || ServerTime >= EndServerTime || !Candidates.Contains(Node))
	{
		return false;
	}
	FRoutePlayerVote* Vote = Votes.FindByPredicate(
		[Player](const FRoutePlayerVote& Entry) { return Player && Entry.Player == Player; });
	if (!Vote || Vote->Node == Node)
	{
		return false;
	}
	Vote->Node = Node;
	return true;
}

int32 FRouteVoteState::CountVotes(FIntPoint Node) const
{
	int32 Count = 0;
	for (const FRoutePlayerVote& Vote : Votes)
	{
		Count += Vote.Node == Node;
	}
	return Count;
}

TArray<FIntPoint> FRouteVoteState::GetWinners(bool bTimeExpired) const
{
	TArray<FIntPoint> Winners;
	if (!bActive || Votes.IsEmpty())
	{
		return Winners;
	}
	if (!bTimeExpired && Votes.ContainsByPredicate(
		[this](const FRoutePlayerVote& Vote) { return !Candidates.Contains(Vote.Node); }))
	{
		return Winners;
	}
	int32 HighestCount = -1;
	for (FIntPoint Node : Candidates)
	{
		const int32 Count = CountVotes(Node);
		if (!bTimeExpired && Count <= Votes.Num() / 2)
		{
			continue;
		}
		if (Count > HighestCount)
		{
			HighestCount = Count;
			Winners.Reset();
		}
		if (Count == HighestCount)
		{
			Winners.Add(Node);
		}
	}
	return Winners;
}
