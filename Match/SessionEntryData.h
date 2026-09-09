#pragma once

#include "CoreMinimal.h"
#include "GameCore/UI/DLPlayerLoadOut.h"

#include "SessionEntryData.generated.h"

// 방 목록 한 줄. 검색 결과 하나를 담는다.
UCLASS(BlueprintType)
class USessionEntryData : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly)
	FString RoomName;

	// 검색 결과에는 인원 수만 오므로 자리 수만큼 빈 항목이 들어간다
	UPROPERTY(BlueprintReadOnly)
	TArray<FDLPlayerLoadout> CurrentPlayers;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 PingMs = 0;

	// FOnlineSessionSearchResult는 BP 노출이 안 되므로 인덱스만 들고 감
	UPROPERTY(BlueprintReadOnly)
	int32 SearchResultIndex = INDEX_NONE;
};
