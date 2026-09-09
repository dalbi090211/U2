#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EventManager.generated.h"

UENUM(BlueprintType)
enum class EScreenIntensity : uint8
{
	Title = 0,
	SubTitle = 1,
	Explain = 2,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnScreenTextRequested, const FText&, Text, EScreenIntensity, Intensity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScreenTextHideRequested, EScreenIntensity, Intensity);

//빈 텍스트면 안내 문구를 숨긴다
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractPromptChanged, const FText&, Prompt);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameOver);

//남은 시간(초). 0 이하면 표시를 지운다
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimeLimitChanged, float, Remaining);

UCLASS()
class UEventManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnScreenTextRequested OnScreenTextRequested;

	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnScreenTextHideRequested OnScreenTextHideRequested;

	//Duration은 호출 시점부터 재는 총 노출 시간이라 페이드 인 길이보다 길어야 한다. 작으면 자동으로 사라지지 않는다. 같은 Intensity로 재호출 시 그 타이머만 새로 시작
	UFUNCTION(BlueprintCallable, Category = "Event")
	void ShowScreenText(const FText& Text, EScreenIntensity Intensity = EScreenIntensity::Explain, float Duration = 3.f);

	UFUNCTION(BlueprintCallable, Category = "Event")
	void HideScreenText(EScreenIntensity Intensity);

	UPROPERTY(BlueprintAssignable, Category = "Interact")
	FOnInteractPromptChanged OnInteractPromptChanged;

	//UInteractComponent가 로컬 플레이어에서만 호출한다
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void SetInteractPrompt(const FText& Prompt);

	//WBP가 Construct에서 바인드한 직후 호출. 위젯보다 먼저 발생한 텍스트를 받아간다
	UFUNCTION(BlueprintCallable, Category = "Event")
	void ReplayActiveTexts();

	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnGameOver OnGameOver;

	UFUNCTION(BlueprintCallable, Category = "Event")
	void BroadcastGameOver() { OnGameOver.Broadcast(); }

	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnTimeLimitChanged OnTimeLimitChanged;

	//제한시간 표시를 켜고 끈다. ABaseGameState가 모든 머신에서 호출한다
	UFUNCTION(BlueprintCallable, Category = "Event")
	void SetTimeLimit(float Remaining);

	//표시 중이 아니면 0
	UFUNCTION(BlueprintPure, Category = "Event")
	float GetTimeLimitRemaining() const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	//맵 이동 시 이전 월드의 문구가 남는 것을 막는다
	void OnPreLoadMap(const FString& MapName);

	TMap<EScreenIntensity, FTimerHandle> HideTimers;

	//지금 화면에 떠 있어야 하는 텍스트
	TMap<EScreenIntensity, FText> ActiveTexts;

	FText InteractPrompt;

	//소수점 자리가 흐르는 감각을 위한 갱신 간격. 초 단위 표시로 바꾸면 1.f로 올린다
	static constexpr float TimeLimitUpdateInterval = 0.05f;

	//위젯이 Tick을 돌 필요 없이 여기서 갱신을 밀어준다
	void TickTimeLimit();

	//제한시간이 끝나는 월드 시각. 0이면 표시 중이 아니다
	double TimeLimitEndTime = 0.0;

	FTimerHandle TimeLimitTickTimer;
};
