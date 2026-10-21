#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MusicDirectorSubsystem.generated.h"

class UFMODEvent;
struct FActorsInitializedParams;
struct FStreamableHandle;
struct FWorldContext;
namespace FMOD::Studio
{
	class EventDescription;
	class EventInstance;
}
namespace FMOD
{
	class DSP;
	class ChannelGroup;
}

UCLASS()
class TEAMPROJECT_FIN_API UMusicDirectorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 네트워크 전달 후 각 머신에서 로컬 음악 상태를 적용한다.
	UFUNCTION(BlueprintCallable, Category = "Audio|Music")
	void SetMusicState(float Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|Music")
	void StopMusic();
	void SetMusicFocus(bool bEnabled);

private:
	enum class EApplyResult : uint8
	{
		Success,
		NotReady,
		Failed
	};

	// FMOD 타입을 헤더에 노출하지 않고 파라미터의 값 데이터만 보관한다.
	struct FParameterCache
	{
		uint32 IdData1 = 0;
		uint32 IdData2 = 0;
		float Minimum = 0.0f;
		float Maximum = 0.0f;
		bool bRequiresInteger = false;
		bool bValid = false;

		bool IsValueAllowed(float Value) const
		{
			return bValid && FMath::IsFinite(Value) && Value >= Minimum && Value <= Maximum &&
				(!bRequiresInteger || Value == FMath::RoundToFloat(Value));
		}
	};

	friend class FProjectMusicOptimizationTest;

	void LoadMainEvent(TSoftObjectPtr<UFMODEvent> Event);
	void OnEventLoaded(uint32 ExpectedGeneration, TSoftObjectPtr<UFMODEvent> Event);
	EApplyResult ApplyState();
	EApplyResult ApplyFocusFilter();
	void ReleaseFocusFilter();
	bool CacheParameter(FMOD::Studio::EventDescription* Description);
	bool RefreshCurrentInstance();
	void ReleaseCurrentInstance();
	void ApplyOrRetry();
	bool RetryPlayback(float DeltaTime);
	void CancelRetry();
	void OnPreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void OnSeamlessTravelStart(UWorld* World, const FString& MapName);
	void ApplyMapMusic(const FActorsInitializedParams& Params);

	UPROPERTY(Transient)
	TObjectPtr<UFMODEvent> MainEvent;

	// 인스턴스의 stop/release는 ReleaseCurrentInstance에서만 수행한다.
	FMOD::Studio::EventInstance* CurrentInstance = nullptr;
	FMOD::DSP* FocusFilter = nullptr;
	FMOD::ChannelGroup* FocusGroup = nullptr;
	bool bMusicFocused = false;
	TSharedPtr<FStreamableHandle> LoadHandle;
	FTSTicker::FDelegateHandle RetryHandle;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle SeamlessTravelHandle;
	FDelegateHandle WorldActorsInitHandle;
	FName ParameterName;
	FParameterCache ParameterCache;
	float DesiredValue = 0.0f;
	float AppliedValue = 0.0f;
	bool bHasDesiredState = false;
	bool bHasAppliedState = false;
	bool bLoading = false;
	uint32 Generation = 0;
	double RetryDeadline = 0.0;
};
