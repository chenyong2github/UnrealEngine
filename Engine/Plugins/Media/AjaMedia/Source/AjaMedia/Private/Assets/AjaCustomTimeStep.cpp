// Copyright Epic Games, Inc. All Rights Reserved.

#include "AjaCustomTimeStep.h"
#include "AjaMediaPrivate.h"
#include "AJA.h"

#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

#include "Misc/App.h"
#include "Misc/ScopeLock.h"


//~ IAJASyncChannelCallbackInterface implementation
//--------------------------------------------------------------------
// Those are called from the AJA thread. There's a lock inside AJA to prevent this object from dying while in this thread.
struct UAjaCustomTimeStep::FAJACallback : public AJA::IAJASyncChannelCallbackInterface
{
	UAjaCustomTimeStep* Owner;
	FAJACallback(UAjaCustomTimeStep* InOwner)
		: Owner(InOwner)
	{}

	//~ IAJAInputCallbackInterface interface
	virtual void OnInitializationCompleted(bool bSucceed) override
	{
		Owner->State = bSucceed ? ECustomTimeStepSynchronizationState::Synchronized : ECustomTimeStepSynchronizationState::Error;
		if (!bSucceed)
		{
			UE_LOG(LogAjaMedia, Error, TEXT("The initialization of '%s' failed. The CustomTimeStep won't be synchronized."), *Owner->GetName());
		}
	}
};


//~ UFixedFrameRateCustomTimeStep implementation
//--------------------------------------------------------------------
UAjaCustomTimeStep::UAjaCustomTimeStep(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bUseReferenceIn(false)
	, TimecodeFormat(EMediaIOTimecodeFormat::LTC)
	, bEnableOverrunDetection(true)
	, SyncChannel(nullptr)
	, SyncCallback(nullptr)
#if WITH_EDITORONLY_DATA
	, InitializedEngine(nullptr)
	, LastAutoSynchronizeInEditorAppTime(0.0)
#endif
	, State(ECustomTimeStepSynchronizationState::Closed)
	, bDidAValidUpdateTimeStep(false)
	, bWarnedAboutVSync(false)
	, bIsPreviousSyncCountValid(false)
	, PreviousSyncCount(0)
{
	MediaConfiguration.bIsInput = !bUseReferenceIn;
}

bool UAjaCustomTimeStep::Initialize(UEngine* InEngine)
{
#if WITH_EDITORONLY_DATA
	InitializedEngine = nullptr;
#endif

	State = ECustomTimeStepSynchronizationState::Closed;
	bDidAValidUpdateTimeStep = false;

	if (!FAja::IsInitialized())
	{
		State = ECustomTimeStepSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Error, TEXT("The CustomTimeStep '%s' can't be initialized. AJA is not initialized on your machine."), *GetName());
		return false;
	}

	if (!FAja::CanUseAJACard())
	{
		State = ECustomTimeStepSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Warning, TEXT("The CustomTimeStep '%s' can't be initialized because Aja card cannot be used. Are you in a Commandlet? You may override this behavior by launching with -ForceAjaUsage"), *GetName());
		return false;
	}

	FString FailureReason;
	if (!MediaConfiguration.IsValid())
	{
		State = ECustomTimeStepSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Error, TEXT("The CustomTimeStep '%s' configuration is invalid."), *GetName());
		return false;
	}

	if (bUseReferenceIn && bWaitForFrameToBeReady)
	{
		UE_LOG(LogAjaMedia, Warning, TEXT("The CustomTimeStep '%s' use both the reference and wait for the frame to be ready. These options are not compatible."), *GetName());
	}

	if (bWaitForFrameToBeReady && MediaConfiguration.MediaMode.Standard == EMediaIOStandardType::Interlaced)
	{
		UE_LOG(LogAjaMedia, Warning, TEXT("The CustomTimeStep '%s' is waiting for the frame to be ready and interlaced picture is not supported."), *GetName());
	}

	check(SyncCallback == nullptr);
	SyncCallback = new FAJACallback(this);

	AJA::AJADeviceOptions DeviceOptions(MediaConfiguration.MediaConnection.Device.DeviceIdentifier);

	//Convert Port Index to match what AJA expects
	AJA::AJASyncChannelOptions Options(*GetName());
	Options.CallbackInterface = SyncCallback;
	Options.ChannelIndex = MediaConfiguration.MediaConnection.PortIdentifier;
	Options.VideoFormatIndex = MediaConfiguration.MediaMode.DeviceModeIdentifier;
	Options.bOutput = bUseReferenceIn;
	Options.bWaitForFrameToBeReady = bWaitForFrameToBeReady && !bUseReferenceIn;
	Options.TransportType = AJA::ETransportType::TT_SdiSingle;
	{
		const EMediaIOTransportType TransportType = MediaConfiguration.MediaConnection.TransportType;
		const EMediaIOQuadLinkTransportType QuadTransportType = MediaConfiguration.MediaConnection.QuadTransportType;
		switch (TransportType)
		{
		case EMediaIOTransportType::SingleLink:
			Options.TransportType = AJA::ETransportType::TT_SdiSingle;
			break;
		case EMediaIOTransportType::DualLink:
			Options.TransportType = AJA::ETransportType::TT_SdiDual;
			break;
		case EMediaIOTransportType::QuadLink:
			Options.TransportType = QuadTransportType == EMediaIOQuadLinkTransportType::SquareDivision ? AJA::ETransportType::TT_SdiQuadSQ : AJA::ETransportType::TT_SdiQuadTSI;
			break;
		case EMediaIOTransportType::HDMI:
			Options.TransportType = AJA::ETransportType::TT_Hdmi;
			break;
		}
	}

	Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_None;
	if (!Options.bOutput)
	{
		switch (TimecodeFormat)
		{
		case EMediaIOTimecodeFormat::None:
			Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_None;
			break;
		case EMediaIOTimecodeFormat::LTC:
			Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_LTC;
			break;
		case EMediaIOTimecodeFormat::VITC:
			Options.TimecodeFormat = AJA::ETimecodeFormat::TCF_VITC1;
			break;
		default:
			break;
		}
	}

	check(SyncChannel == nullptr);
	SyncChannel = new AJA::AJASyncChannel();
	if (!SyncChannel->Initialize(DeviceOptions, Options))
	{
		State = ECustomTimeStepSynchronizationState::Error;
		delete SyncChannel;
		SyncChannel = nullptr;
		delete SyncCallback;
		SyncCallback = nullptr;
		return false;
	}

#if WITH_EDITORONLY_DATA
	InitializedEngine = InEngine;
#endif

	State = ECustomTimeStepSynchronizationState::Synchronizing;
	return true;
}

void UAjaCustomTimeStep::Shutdown(UEngine* InEngine)
{
#if WITH_EDITORONLY_DATA
	InitializedEngine = nullptr;
#endif

	State = ECustomTimeStepSynchronizationState::Closed;
	ReleaseResources();
}

bool UAjaCustomTimeStep::UpdateTimeStep(UEngine* InEngine)
{
	bool bRunEngineTimeStep = true;
	if (State == ECustomTimeStepSynchronizationState::Synchronized)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		if (!bWarnedAboutVSync)
		{
			bool bLockToVsync = CVar->GetValueOnGameThread() != 0;
			if (bLockToVsync)
			{
				UE_LOG(LogAjaMedia, Warning, TEXT("The Engine is using VSync and the AJACustomTimeStep. It may break the 'genlock'."));
				bWarnedAboutVSync = true;
			}
		}

		// Updates logical last time to match logical current time from last tick
		UpdateApplicationLastTime();

		const double BeforeTime = FPlatformTime::Seconds();

		WaitForSync();

		// Use fixed delta time and update time.
		FApp::SetCurrentTime(FPlatformTime::Seconds());
		FApp::SetIdleTime(FApp::GetCurrentTime() - BeforeTime);

		double InterlacedMultiplier = ((MediaConfiguration.MediaMode.Standard == EMediaIOStandardType::Interlaced) && bWaitForFrameToBeReady) ? 2.0 : 1.0;
		FApp::SetDeltaTime(GetFixedFrameRate().AsInterval() * InterlacedMultiplier);

		bRunEngineTimeStep = false;
		bDidAValidUpdateTimeStep = true;
	}
	else if (State == ECustomTimeStepSynchronizationState::Error)
	{
		ReleaseResources();

		// In Editor only, when not in pie, reinitialized the device
#if WITH_EDITORONLY_DATA && WITH_EDITOR
		if (InitializedEngine && !GIsPlayInEditorWorld && GIsEditor)
		{
			const double TimeBetweenAttempt = 1.0;
			if (FApp::GetCurrentTime() - LastAutoSynchronizeInEditorAppTime > TimeBetweenAttempt)
			{
				Initialize(InitializedEngine);
				LastAutoSynchronizeInEditorAppTime = FApp::GetCurrentTime();
			}
		}
#endif
	}

	return bRunEngineTimeStep;
}

ECustomTimeStepSynchronizationState UAjaCustomTimeStep::GetSynchronizationState() const
{
	if (State == ECustomTimeStepSynchronizationState::Synchronized)
	{
		return bDidAValidUpdateTimeStep ? ECustomTimeStepSynchronizationState::Synchronized : ECustomTimeStepSynchronizationState::Synchronizing;
	}
	return State;
}

FFrameRate UAjaCustomTimeStep::GetFixedFrameRate() const
{
	return MediaConfiguration.MediaMode.FrameRate;
}

//~ UObject implementation
//--------------------------------------------------------------------
void UAjaCustomTimeStep::BeginDestroy()
{
	ReleaseResources();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UAjaCustomTimeStep::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAjaCustomTimeStep, bUseReferenceIn))
	{
		MediaConfiguration = FMediaIOConfiguration();
		MediaConfiguration.bIsInput = !bUseReferenceIn;
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

//~ UAjaCustomTimeStep implementation
//--------------------------------------------------------------------
void UAjaCustomTimeStep::WaitForSync()
{
	check(SyncChannel);

	bool bWaitIsValid = SyncChannel->WaitForSync();
	if (bEnableOverrunDetection && bWaitIsValid)
	{
		uint32 NewSyncCount = 0;
		bool bIsNewSyncCountValid = SyncChannel->GetSyncCount(NewSyncCount);

		if (bIsNewSyncCountValid && bIsPreviousSyncCountValid && NewSyncCount != PreviousSyncCount+1)
		{
			UE_LOG(LogAjaMedia, Warning, TEXT("The Engine couldn't run fast enough to keep up with the CustomTimeStep Sync. '%d' frame(s) was dropped."), NewSyncCount-PreviousSyncCount-1);
		}
		bIsPreviousSyncCountValid = bIsNewSyncCountValid;
		PreviousSyncCount = NewSyncCount;
	}

	if (!bWaitIsValid)
	{
		State = ECustomTimeStepSynchronizationState::Error;
		UE_LOG(LogAjaMedia, Error, TEXT("The Engine couldn't run fast enough to keep up with the CustomTimeStep Sync. The wait timeout."));
	}
}

void UAjaCustomTimeStep::ReleaseResources()
{
	if (SyncChannel)
	{
		SyncChannel->Uninitialize();
		delete SyncChannel;
		SyncChannel = nullptr;
		delete SyncCallback;
		SyncCallback = nullptr;
	}

	bWarnedAboutVSync = false;
	bIsPreviousSyncCountValid = false;
}

