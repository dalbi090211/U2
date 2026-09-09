#pragma once

#include "CoreMinimal.h"
#include "GameCore/BasePlayerController.h"
#include "GameCore/UI/DLPlayerLoadOut.h"
#include "Interfaces/OnlineSessionInterface.h"

#include "LobbyPlayerController.generated.h"

class USessionEntryData;
class URoomPlayerEntryData;
class FOnlineSessionSearch;

// 방 목록 갱신됨. 위젯이 여기 바인딩해서 리스트를 다시 그린다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSessionListUpdated);

// 대기실 인원/영웅 선택이 바뀜. 대기실 UI가 여기 바인딩한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomUpdatedBP);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomCreated, bool, bSuccess);

// 참가 절차가 시작됐다. 목록을 잠그는 데 쓴다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomJoinStarted);

// 참가 실패 시에만 도달한다
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomJoined, bool, bSuccess);

UCLASS()
class ALobbyPlayerController : public ABasePlayerController
{
	GENERATED_BODY()

public:
	// [Create Game] 버튼. 세션을 만들고 현재 맵에서 리슨 서버를 연다
	UFUNCTION(BlueprintCallable, Category = "Match")
	void CreateGame(const FString& RoomName);

	// [Refresh] 버튼 / 로비 진입 시
	UFUNCTION(BlueprintCallable, Category = "Match")
	void FindGames();

	// [Join] 버튼. 세션 참가 후 호스트 월드로 접속한다.
	UFUNCTION(BlueprintCallable, Category = "Match")
	bool JoinGame(USessionEntryData* Entry);

	// 대기실 영웅 선택. 서버 RPC로 넘어간다
	UFUNCTION(BlueprintCallable, Category = "Match")
	void SelectHero(EDLHero NewHero);

	// [Start] 버튼. 호스트만. 전원이 게임 맵으로 함께 이동한다.
	UFUNCTION(BlueprintCallable, Category = "Match")
	void StartGame();

	// 대기실에서 나갈 때
	UFUNCTION(BlueprintCallable, Category = "Match")
	void LeaveRoom();

	// 대기실 인원 목록. 원본은 GameState의 PlayerArray다.
	UFUNCTION(BlueprintCallable, Category = "Match")
	TArray<UObject*> GetRoomPlayerEntries();

	UFUNCTION(BlueprintPure, Category = "Match")
	bool IsHost() const;

	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnSessionListUpdated OnSessionListUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnRoomUpdatedBP OnRoomUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnRoomCreated OnRoomCreated;

	/*
	 * JoinGame이 실제로 참가를 시작할 때 발화한다. 목록을 비활성화하는 용도.
	 * 되돌리는 짝은 OnRoomJoined(false)다. 성공하면 트래블로 월드가 통째로 바뀌므로 되돌릴 필요가 없다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnRoomJoinStarted OnRoomJoinStarted;

	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnRoomJoined OnRoomJoined;

	UPROPERTY(BlueprintReadOnly, Category = "Match")
	TArray<TObjectPtr<USessionEntryData>> FoundSessions;

	UPROPERTY(EditDefaultsOnly, Category = "Match")
	int32 MaxPlayers = 4;

	// 방 목록 필터용 식별자. 뒤의 버전은 네트워크 호환이 깨질 때 올린다
	UPROPERTY(EditDefaultsOnly, Category = "Match")
	FString GameKey = TEXT("TeamProject3_v1");

	// 나가기를 누르면 돌아올 맵
	UPROPERTY(EditDefaultsOnly, Category = "Match")
	FString LobbyMapName = TEXT("LobbyTestMap");

	// 시작을 누르면 전원이 이동할 맵
	UPROPERTY(EditDefaultsOnly, Category = "Match")
	FString GameplayMapName = TEXT("PCGTestMap");

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void Server_SelectHero(EDLHero NewHero);

	// GameState의 인원 변경 신호를 BP 델리게이트로 넘긴다.
	UFUNCTION()
	void OnRoomUpdated_Broadcast();

private:
	IOnlineSessionPtr GetSessionInterface() const;

	void OnCreateComplete(FName SessionName, bool bSuccess);
	void OnFindComplete(bool bSuccess);
	void OnJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);

	// GameState는 클라에서 늦게 도착한다. 도착하면 인원 변경 신호에 붙는다.
	void BindRoomUpdates();

	void ClearDelegates();

	TSharedPtr<FOnlineSessionSearch> Search;

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;

	FString PendingRoomName;
};
