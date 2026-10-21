#include "GameCore/Audio/MusicDirectorSubsystem.h"

#include "Containers/Ticker.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "FMODEvent.h"
#include "FMODStudioModule.h"
#include "GameCore/Audio/MapMusicTable.h"
#include "GameCore/Audio/ProjectAudioSettings.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/UObjectGlobals.h"
#include "fmod_errors.h"
#include "fmod_studio.hpp"
#include "fmod.hpp"

DEFINE_LOG_CATEGORY_STATIC(LogProjectMusic, Log, All);

namespace
{
	constexpr float MusicRetryInterval = 0.1f;
	constexpr double MusicRetryDuration = 10.0;
}

void UMusicDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::OnPreLoadMap);
	// 심리스 이동은 PreLoadMap 을 안 보낸다. 안 걸면 이전 맵 BGM 이 새 맵까지 따라온다
	SeamlessTravelHandle = FWorldDelegates::OnSeamlessTravelStart.AddUObject(this, &ThisClass::OnSeamlessTravelStart);

	// PostLoadMap 은 PIE 첫 맵에서 안 온다. 액터 초기화는 PIE, OpenLevel, 심리스 이동 모두 거친다
	WorldActorsInitHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &ThisClass::ApplyMapMusic);
}

void UMusicDirectorSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
	FWorldDelegates::OnSeamlessTravelStart.Remove(SeamlessTravelHandle);
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldActorsInitHandle);
	StopMusic();
	Super::Deinitialize();
}

void UMusicDirectorSubsystem::SetMusicState(float Value)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectMusic_SetMusicState);
	const UWorld* World = GetWorld();
	if (IsRunningDedicatedServer() || (World && World->GetNetMode() == NM_DedicatedServer))
	{
		return;
	}
	if (!FMath::IsFinite(Value))
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("음악 상태 값은 유한한 숫자여야 합니다."));
		return;
	}
	DesiredValue = Value;
	bHasDesiredState = true;
	// 진행 중인 작업은 유지하고 마지막 요청값만 갱신한다. 대기 제한도 연장하지 않는다.
	if (bLoading || RetryHandle.IsValid())
	{
		return;
	}
	const UProjectAudioSettings* Settings = GetDefault<UProjectAudioSettings>();
	if (ParameterName != Settings->MusicStateParameterName)
	{
		ParameterCache = FParameterCache();
		bHasAppliedState = false;
	}
	ParameterName = Settings->MusicStateParameterName;
	if (Settings->MainMusicEvent.IsNull() || ParameterName.IsNone())
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("MainMusicEvent 또는 MusicStateParameterName이 설정되지 않았습니다."));
		return;
	}
	if (!MainEvent)
	{
		LoadMainEvent(Settings->MainMusicEvent);
		return;
	}
	ApplyOrRetry();
}

void UMusicDirectorSubsystem::LoadMainEvent(TSoftObjectPtr<UFMODEvent> Event)
{
	bLoading = true;
	const uint32 ExpectedGeneration = Generation;
	// 완료 시 설정을 다시 조회하지 않고 실제로 요청한 에셋을 사용한다.
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Event.ToSoftObjectPath(),
		FStreamableDelegate::CreateWeakLambda(this, [this, ExpectedGeneration, Event]()
		{
			OnEventLoaded(ExpectedGeneration, Event);
		}));
	if (!LoadHandle)
	{
		bLoading = false;
		UE_LOG(LogProjectMusic, Warning, TEXT("메인 FMOD Event 로딩 요청 실패."));
	}
}

void UMusicDirectorSubsystem::OnEventLoaded(uint32 ExpectedGeneration, TSoftObjectPtr<UFMODEvent> Event)
{
	if (ExpectedGeneration != Generation || !bHasDesiredState)
	{
		return;
	}
	bLoading = false;
	MainEvent = Event.Get();
	LoadHandle.Reset();
	if (!MainEvent)
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("메인 FMOD Event 로딩 실패."));
		return;
	}
	ApplyOrRetry();
}

UMusicDirectorSubsystem::EApplyResult UMusicDirectorSubsystem::ApplyState()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectMusic_ApplyState);
	if (!IFMODStudioModule::IsAvailable() ||
		!IFMODStudioModule::Get().GetStudioSystem(EFMODSystemContext::Runtime))
	{
		return EApplyResult::NotReady;
	}
	if (!RefreshCurrentInstance())
	{
		return EApplyResult::Failed;
	}

	// 정상 인스턴스의 후속 요청은 Event 조회 및 문자열 변환 없이 처리한다.
	FMOD::Studio::EventDescription* Description = nullptr;
	if (!CurrentInstance || !ParameterCache.bValid)
	{
		Description = IFMODStudioModule::Get().GetEventDescription(MainEvent, EFMODSystemContext::Runtime);
		if (!Description)
		{
			return EApplyResult::NotReady;
		}
		if (!ParameterCache.bValid && !CacheParameter(Description))
		{
			return EApplyResult::Failed;
		}
	}
	if (!ParameterCache.IsValueAllowed(DesiredValue))
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("음악 파라미터 값이 잘못되었습니다: %s = %f (범위 %f~%f)"),
			*ParameterName.ToString(), DesiredValue, ParameterCache.Minimum, ParameterCache.Maximum);
		return EApplyResult::Failed;
	}

	const bool bNewInstance = CurrentInstance == nullptr;
	if (!bNewInstance && bHasAppliedState && AppliedValue == DesiredValue)
	{
		return ApplyFocusFilter();
	}

	if (bNewInstance)
	{
		const FMOD_RESULT Result = Description->createInstance(&CurrentInstance);
		if (Result != FMOD_OK)
		{
			CurrentInstance = nullptr;
			UE_LOG(LogProjectMusic, Warning, TEXT("음악 인스턴스 생성 실패: %s"), UTF8_TO_TCHAR(FMOD_ErrorString(Result)));
			return EApplyResult::Failed;
		}
	}

	// 최초 재생만 Seek Speed를 건너뛰고, 이후 전환은 FMOD 설정을 따른다.
	const FMOD_STUDIO_PARAMETER_ID ParameterId{ ParameterCache.IdData1, ParameterCache.IdData2 };
	FMOD_RESULT Result = CurrentInstance->setParameterByID(ParameterId, DesiredValue, bNewInstance);
	if (Result == FMOD_OK && bNewInstance)
	{
		Result = CurrentInstance->start();
	}
	if (Result != FMOD_OK)
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("음악 상태 적용/재생 실패: %s"), UTF8_TO_TCHAR(FMOD_ErrorString(Result)));
		// Bank 재로드 등으로 ID가 무효화된 경우 다음 요청에서 다시 조회한다.
		ParameterCache = FParameterCache();
		bHasAppliedState = false;
		if (bNewInstance)
		{
			ReleaseCurrentInstance();
		}
		return EApplyResult::Failed;
	}
	AppliedValue = DesiredValue;
	bHasAppliedState = true;
	return ApplyFocusFilter();
}

void UMusicDirectorSubsystem::SetMusicFocus(bool bEnabled)
{
	bMusicFocused = bEnabled;
	if (!bEnabled)
	{
		ReleaseFocusFilter();
	}
	else if (CurrentInstance && bHasDesiredState)
	{
		ApplyOrRetry();
	}
}

UMusicDirectorSubsystem::EApplyResult UMusicDirectorSubsystem::ApplyFocusFilter()
{
	if (!bMusicFocused || FocusFilter)
	{
		return EApplyResult::Success;
	}
	FMOD::ChannelGroup* Group = nullptr;
	FMOD_RESULT Result = CurrentInstance->getChannelGroup(&Group);
	// FMOD가 비동기로 채널을 만드는 동안은 기존 음악 재시도 예약을 사용한다.
	if (Result == FMOD_ERR_STUDIO_NOT_LOADED)
	{
		return EApplyResult::NotReady;
	}
	FMOD::System* Core = nullptr;
	if (Result == FMOD_OK)
	{
		Result = IFMODStudioModule::Get().GetStudioSystem(EFMODSystemContext::Runtime)->getCoreSystem(&Core);
	}
	if (Result == FMOD_OK)
	{
		Result = Core->createDSPByType(FMOD_DSP_TYPE_LOWPASS_SIMPLE, &FocusFilter);
	}
	if (Result == FMOD_OK)
	{
		Result = FocusFilter->setParameterFloat(FMOD_DSP_LOWPASS_SIMPLE_CUTOFF, 1500.0f);
	}
	if (Result == FMOD_OK)
	{
		Result = Group->addDSP(FMOD_CHANNELCONTROL_DSP_HEAD, FocusFilter);
	}
	if (Result != FMOD_OK)
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("BGM 집중 필터 적용 실패: %s"), UTF8_TO_TCHAR(FMOD_ErrorString(Result)));
		ReleaseFocusFilter();
		return EApplyResult::Failed;
	}
	FocusGroup = Group;
	return EApplyResult::Success;
}

void UMusicDirectorSubsystem::ReleaseFocusFilter()
{
	if (FocusFilter)
	{
		if (FocusGroup)
		{
			const FMOD_RESULT Result = FocusGroup->removeDSP(FocusFilter);
			if (Result != FMOD_OK && Result != FMOD_ERR_INVALID_HANDLE)
			{
				UE_LOG(LogProjectMusic, Warning, TEXT("BGM 집중 필터 분리 실패: %s"), UTF8_TO_TCHAR(FMOD_ErrorString(Result)));
			}
			FocusGroup = nullptr;
		}
		const FMOD_RESULT Result = FocusFilter->release();
		if (Result != FMOD_OK)
		{
			UE_LOG(LogProjectMusic, Warning, TEXT("BGM 집중 필터 해제 실패: %s"), UTF8_TO_TCHAR(FMOD_ErrorString(Result)));
		}
		FocusFilter = nullptr;
	}
}

bool UMusicDirectorSubsystem::CacheParameter(FMOD::Studio::EventDescription* Description)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectMusic_CacheParameter);
	const FTCHARToUTF8 Name(*ParameterName.ToString());
	FMOD_STUDIO_PARAMETER_DESCRIPTION Parameter{};
	const FMOD_RESULT Result = Description->getParameterDescriptionByName(Name.Get(), &Parameter);
	if (Result != FMOD_OK)
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("음악 파라미터 조회 실패 (%s): %s"),
			*ParameterName.ToString(), UTF8_TO_TCHAR(FMOD_ErrorString(Result)));
		return false;
	}
	if (Parameter.type != FMOD_STUDIO_PARAMETER_GAME_CONTROLLED ||
		(Parameter.flags & (FMOD_STUDIO_PARAMETER_GLOBAL | FMOD_STUDIO_PARAMETER_READONLY)))
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("쓰기 가능한 로컬 음악 파라미터가 아닙니다: %s"), *ParameterName.ToString());
		return false;
	}
	ParameterCache.IdData1 = Parameter.id.data1;
	ParameterCache.IdData2 = Parameter.id.data2;
	ParameterCache.Minimum = Parameter.minimum;
	ParameterCache.Maximum = Parameter.maximum;
	ParameterCache.bRequiresInteger =
		(Parameter.flags & (FMOD_STUDIO_PARAMETER_DISCRETE | FMOD_STUDIO_PARAMETER_LABELED)) != 0;
	ParameterCache.bValid = true;
	return true;
}

bool UMusicDirectorSubsystem::RefreshCurrentInstance()
{
	if (CurrentInstance && !CurrentInstance->isValid())
	{
		ReleaseFocusFilter();
		CurrentInstance = nullptr;
		ParameterCache = FParameterCache();
		bHasAppliedState = false;
	}
	if (CurrentInstance)
	{
		FMOD_STUDIO_PLAYBACK_STATE State = FMOD_STUDIO_PLAYBACK_STOPPED;
		const FMOD_RESULT Result = CurrentInstance->getPlaybackState(&State);
		if (Result != FMOD_OK)
		{
			UE_LOG(LogProjectMusic, Warning, TEXT("음악 재생 상태 조회 실패: %s"), UTF8_TO_TCHAR(FMOD_ErrorString(Result)));
			return false;
		}
		if (State == FMOD_STUDIO_PLAYBACK_STOPPED)
		{
			ReleaseCurrentInstance();
		}
	}
	return true;
}

void UMusicDirectorSubsystem::ReleaseCurrentInstance()
{
	ReleaseFocusFilter();
	if (CurrentInstance && IFMODStudioModule::IsAvailable() && CurrentInstance->isValid())
	{
		CurrentInstance->stop(FMOD_STUDIO_STOP_IMMEDIATE);
		CurrentInstance->release();
	}
	CurrentInstance = nullptr;
	ParameterCache = FParameterCache();
	bHasAppliedState = false;
}

void UMusicDirectorSubsystem::ApplyOrRetry()
{
	if (ApplyState() != EApplyResult::NotReady)
	{
		CancelRetry();
		return;
	}
	if (RetryHandle.IsValid())
	{
		return;
	}
	RetryDeadline = FPlatformTime::Seconds() + MusicRetryDuration;
	RetryHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::RetryPlayback), MusicRetryInterval);
}

bool UMusicDirectorSubsystem::RetryPlayback(float DeltaTime)
{
	const EApplyResult Result = ApplyState();
	if (Result != EApplyResult::NotReady)
	{
		RetryHandle.Reset();
		return false;
	}
	if (FPlatformTime::Seconds() >= RetryDeadline)
	{
		UE_LOG(LogProjectMusic, Warning, TEXT("FMOD 시스템/Bank 준비 대기 시간 초과. 다음 음악 이벤트에서 재시도합니다."));
		RetryHandle.Reset();
		return false;
	}
	return true;
}

void UMusicDirectorSubsystem::CancelRetry()
{
	if (RetryHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
	}
	RetryHandle.Reset();
	RetryDeadline = 0.0;
}

void UMusicDirectorSubsystem::StopMusic()
{
	++Generation;
	CancelRetry();
	if (LoadHandle)
	{
		LoadHandle->CancelHandle();
	}
	LoadHandle.Reset();
	bLoading = false;
	bHasDesiredState = false;
	ReleaseCurrentInstance();
	MainEvent = nullptr;
}

void UMusicDirectorSubsystem::OnPreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	if (WorldContext.OwningGameInstance == GetGameInstance())
	{
		StopMusic();
	}
}

void UMusicDirectorSubsystem::OnSeamlessTravelStart(UWorld* World, const FString& MapName)
{
	if (World && World->GetGameInstance() == GetGameInstance())
	{
		StopMusic();
	}
}

void UMusicDirectorSubsystem::ApplyMapMusic(const FActorsInitializedParams& Params)
{
	const UProjectAudioSettings* Settings = GetDefault<UProjectAudioSettings>();
	if (Params.World->GetGameInstance() != GetGameInstance() || Settings->MapMusicTable.IsNull())
	{
		return;
	}

	const UMapMusicTable* Table = Settings->MapMusicTable.LoadSynchronous();
	checkf(Table, TEXT("[Music] 맵 음악 표 로드 실패: %s"), *Settings->MapMusicTable.ToString());

	// PIE 월드는 패키지 이름에 UEDPIE_0_ 이 붙어 있어 그대로는 표의 맵과 안 맞는다
	const FString MapName = UWorld::RemovePIEPrefix(Params.World->GetPackage()->GetName());

	for (const TPair<TSoftObjectPtr<UWorld>, TObjectPtr<UMusicParameterEvent>>& Entry : Table->MapMusic)
	{
		if (Entry.Key.ToSoftObjectPath().GetLongPackageName() != MapName)
		{
			continue;
		}

		checkf(Entry.Value, TEXT("[Music] 맵 음악 표에 음악이 빈 칸: %s"), *MapName);
		SetMusicState(Entry.Value->ParameterValue);
		return;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProjectMusicOptimizationTest,
	"TeamProject.Audio.MusicOptimization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectMusicOptimizationTest::RunTest(const FString& Parameters)
{
	// 값 검증이 FMOD 호출 없이 동작하고 기존 Discrete/Continuous 규칙을 보존하는지 검사한다.
	UMusicDirectorSubsystem::FParameterCache Cache;
	TestFalse(TEXT("조회 전 캐시는 사용할 수 없음"), Cache.IsValueAllowed(0.0f));
	Cache.bValid = true;
	Cache.Minimum = 0.0f;
	Cache.Maximum = 10.0f;
	Cache.bRequiresInteger = true;
	TestTrue(TEXT("최솟값 허용"), Cache.IsValueAllowed(0.0f));
	TestTrue(TEXT("최댓값 허용"), Cache.IsValueAllowed(10.0f));
	TestFalse(TEXT("범위 미만 거부"), Cache.IsValueAllowed(-1.0f));
	TestFalse(TEXT("범위 초과 거부"), Cache.IsValueAllowed(11.0f));
	TestFalse(TEXT("Discrete 소수 거부"), Cache.IsValueAllowed(1.5f));
	Cache.bRequiresInteger = false;
	TestTrue(TEXT("Continuous 소수 허용"), Cache.IsValueAllowed(1.5f));

	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UMusicDirectorSubsystem* Director = NewObject<UMusicDirectorSubsystem>(GameInstance);
	Director->bLoading = true;
	Director->SetMusicState(1.0f);
	Director->SetMusicState(9.0f);
	TestEqual(TEXT("로딩 중 최신 값 유지"), Director->DesiredValue, 9.0f);
	TestTrue(TEXT("로딩 중 중복 로딩 요청 없음"), Director->bLoading && !Director->LoadHandle);
	Director->StopMusic();

	// 아직 준비되지 않은 시스템을 흉내 내되 실제 FMOD 및 오디오 장치에는 접근하지 않는다.
	Director->RetryDeadline = FPlatformTime::Seconds() + MusicRetryDuration;
	const double OriginalDeadline = Director->RetryDeadline;
	Director->RetryHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) { return false; }), 3600.0f);
	const auto OriginalRetryHandle = Director->RetryHandle.Pin();
	Director->SetMusicState(2.0f);
	Director->SetMusicState(8.0f);
	TestTrue(TEXT("대기 중 재시도 예약 재사용"), Director->RetryHandle.Pin() == OriginalRetryHandle);
	TestEqual(TEXT("반복 요청이 대기 제한을 연장하지 않음"), Director->RetryDeadline, OriginalDeadline);
	TestEqual(TEXT("대기 중 최신 값 유지"), Director->DesiredValue, 8.0f);

	Director->ParameterCache = Cache;
	Director->bHasAppliedState = true;
	const uint32 PreviousGeneration = Director->Generation;
	Director->StopMusic();
	TestFalse(TEXT("정지 시 재시도 예약 해제"), Director->RetryHandle.IsValid());
	TestFalse(TEXT("정지 시 파라미터 캐시 해제"), Director->ParameterCache.bValid);
	TestFalse(TEXT("정지 시 적용 상태 해제"), Director->bHasAppliedState);
	TestFalse(TEXT("정지 시 요청 상태 해제"), Director->bHasDesiredState);
	Director->SetMusicFocus(true);
	TestTrue(TEXT("음악 로딩 전 집중 상태 유지"), Director->bMusicFocused);
	TestNull(TEXT("음악 인스턴스 없이 필터 생성 안 함"), Director->FocusFilter);
	Director->SetMusicFocus(false);
	TestFalse(TEXT("음악 로딩 전에도 집중 해제"), Director->bMusicFocused);
	TestEqual(TEXT("정지 시 이전 비동기 세대 무효화"), Director->Generation, PreviousGeneration + 1);
	return true;
}
#endif
