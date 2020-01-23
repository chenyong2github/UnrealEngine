// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlackmagicCustomTimeStep.h"

#include "Blackmagic.h"
#include "BlackmagicMediaPrivate.h"

#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

#include "Misc/App.h"
#include "Templates/Atomic.h"


namespace BlackmagicCustomTimeStepHelpers
{
	//~ FInputEventCallback implementation
	//--------------------------------------------------------------------
	class FInputEventCallback : public BlackmagicDesign::IInputEventCallback
	{
	public:
		FInputEventCallback(const BlackmagicDesign::FChannelInfo& InChannelInfo, bool bInEnableOverrunDetection)
			: RefCounter(0)
			, ChannelInfo(InChannelInfo)
			, State(ECustomTimeStepSynchronizationState::Closed)
			, WaitSyncEvent(nullptr)
			, bWaitedOnce(false)
			, bEnableOverrunDetection(bInEnableOverrunDetection)
			, bIsPreviousSyncCountValid(false)
			, PreviousSyncCount(0)
			, CurrentSyncCount(0)
		{
		}

		virtual ~FInputEventCallback()
		{
			if (WaitSyncEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(WaitSyncEvent);
			}
		}
	

		bool Initialize(const BlackmagicDesign::FInputChannelOptions& InChannelInfo)
		{
			AddRef();

			BlackmagicDesign::ReferencePtr<BlackmagicDesign::IInputEventCallback> SelfRef(this);
			BlackmagicIdendifier = BlackmagicDesign::RegisterCallbackForChannel(ChannelInfo, InChannelInfo, SelfRef);
			State = BlackmagicIdendifier.IsValid() ? ECustomTimeStepSynchronizationState::Synchronizing : ECustomTimeStepSynchronizationState::Error;
			return BlackmagicIdendifier.IsValid();
		}

		void Uninitialize()
		{
			if (BlackmagicIdendifier.IsValid())
			{
				BlackmagicDesign::UnregisterCallbackForChannel(ChannelInfo, BlackmagicIdendifier);
				BlackmagicIdendifier = BlackmagicDesign::FUniqueIdentifier();
			}

			Release();
		}

		ECustomTimeStepSynchronizationState GetSynchronizationState() const { return State; }

		bool WaitForSync()
		{
			bool bResult = false;
			if (WaitSyncEvent && State == ECustomTimeStepSynchronizationState::Synchronized)
			{
				uint32 NumberOfMilliseconds = 100;
				bResult = WaitSyncEvent->Wait(NumberOfMilliseconds);
				uint64 LocalCurrentSyncCount = CurrentSyncCount;
				if (!bResult)
				{
					State = ECustomTimeStepSynchronizationState::Error;
				}
				else if (bEnableOverrunDetection && bIsPreviousSyncCountValid && LocalCurrentSyncCount != PreviousSyncCount+1)
				{
					UE_LOG(LogBlackmagicMedia, Warning, TEXT("The Engine couldn't run fast enough to keep up with the CustomTimeStep Sync. '%d' frame(s) was dropped."), CurrentSyncCount - PreviousSyncCount + 1);
				}

				bIsPreviousSyncCountValid = bResult;
				PreviousSyncCount = LocalCurrentSyncCount;
			}
			return bResult;
		}

	private:
		virtual void AddRef() override
		{
			++RefCounter;
		}

		virtual void Release() override
		{
			--RefCounter;
			if (RefCounter == 0)
			{
				delete this;
			}
		}
		virtual void OnInitializationCompleted(bool bSuccess) override
		{
			if (bSuccess)
			{
				const bool bIsManualReset = false;
				WaitSyncEvent = FPlatformProcess::GetSynchEventFromPool(bIsManualReset);
			}
			else
			{
				State = ECustomTimeStepSynchronizationState::Error;
			}
		}

		virtual void OnShutdownCompleted() override
		{
			State = ECustomTimeStepSynchronizationState::Closed;
			if (WaitSyncEvent)
			{
				WaitSyncEvent->Trigger();
			}
		}

		virtual void OnFrameReceived(const BlackmagicDesign::IInputEventCallback::FFrameReceivedInfo& InFrameInfo) override
		{
			if (!bWaitedOnce)
			{
				bWaitedOnce = true;
				State = ECustomTimeStepSynchronizationState::Synchronized;
			}

			CurrentSyncCount = InFrameInfo.FrameNumber;

			if (WaitSyncEvent)
			{
				WaitSyncEvent->Trigger();
			}
		}

		virtual void OnFrameFormatChanged(const BlackmagicDesign::FFormatInfo& NewFormat) override
		{
			UE_LOG(LogBlackmagicMedia, Error, TEXT("The video format changed."));
			State = ECustomTimeStepSynchronizationState::Error;
			if (WaitSyncEvent)
			{
				WaitSyncEvent->Trigger();
			}
		}

		virtual void OnInterlacedOddFieldEvent() override
		{
			if (WaitSyncEvent)
			{
				WaitSyncEvent->Trigger();
			}
		}

	private:
		TAtomic<int32> RefCounter;

		BlackmagicDesign::FUniqueIdentifier BlackmagicIdendifier;
		BlackmagicDesign::FChannelInfo ChannelInfo;

		ECustomTimeStepSynchronizationState State;

		FEvent* WaitSyncEvent;
		bool bWaitedOnce;

		bool bEnableOverrunDetection;
		bool bIsPreviousSyncCountValid;
		uint64 PreviousSyncCount;
		TAtomic<uint64> CurrentSyncCount;
	};
}

UBlackmagicCustomTimeStep::UBlackmagicCustomTimeStep(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bEnableOverrunDetection(false)
	, InputEventCallback(nullptr)
	, bWarnedAboutVSync(false)
{
	MediaConfiguration.bIsInput = true;
}

bool UBlackmagicCustomTimeStep::Initialize(class UEngine* InEngine)
{
	if (!MediaConfiguration.IsValid())
	{
		UE_LOG(LogBlackmagicMedia, Warning, TEXT("The configuration of '%s' is not valid."), *GetName());
		return false;
	}

	check(InputEventCallback == nullptr);

	if (!FBlackmagic::IsInitialized())
	{
		UE_LOG(LogBlackmagicMedia, Error, TEXT("The CustomTimeStep '%s' can't be initialized. Blackmagic is not initialized on your machine."), *GetName());
		return false;
	}

	if (!FBlackmagic::CanUseBlackmagicCard())
	{
		UE_LOG(LogBlackmagicMedia, Warning, TEXT("The CustomTimeStep '%s' can't be initialized because Blackmagic card cannot be used. Are you in a Commandlet? You may override this behavior by launching with -ForceBlackmagicUsage"), *GetName());
		return false;
	}

	BlackmagicDesign::FChannelInfo ChannelInfo;
	ChannelInfo.DeviceIndex = MediaConfiguration.MediaConnection.Device.DeviceIdentifier;

	InputEventCallback = new BlackmagicCustomTimeStepHelpers::FInputEventCallback(ChannelInfo, bEnableOverrunDetection);

	BlackmagicDesign::FInputChannelOptions ChannelOptions;
	ChannelOptions.CallbackPriority = 1;
	ChannelOptions.FormatInfo.DisplayMode = MediaConfiguration.MediaMode.DeviceModeIdentifier;
	ChannelOptions.TimecodeFormat = BlackmagicDesign::ETimecodeFormat::TCF_None;

	bool bResult = InputEventCallback->Initialize(ChannelOptions);
	if (!bResult)
	{
		ReleaseResources();
	}

	return bResult;
}

void UBlackmagicCustomTimeStep::Shutdown(class UEngine* InEngine)
{
	ReleaseResources();
}

bool UBlackmagicCustomTimeStep::UpdateTimeStep(class UEngine* InEngine)
{
	bool bRunEngineTimeStep = true;

	const ECustomTimeStepSynchronizationState CurrentState = GetSynchronizationState();
	if (CurrentState == ECustomTimeStepSynchronizationState::Synchronized)
	{
		static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		if (!bWarnedAboutVSync)
		{
			bool bLockToVsync = CVar->GetValueOnGameThread() != 0;
			if (bLockToVsync)
			{
				UE_LOG(LogBlackmagicMedia, Warning, TEXT("The Engine is using VSync and the BlackmagicCustomTimeStep. It may break the 'genlock'."));
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
		FApp::SetDeltaTime(GetFixedFrameRate().AsInterval());

		bRunEngineTimeStep = false;
	}
	else if (CurrentState == ECustomTimeStepSynchronizationState::Error)
	{
		ReleaseResources();
	}

	return bRunEngineTimeStep;
}

ECustomTimeStepSynchronizationState UBlackmagicCustomTimeStep::GetSynchronizationState() const
{
	if (InputEventCallback)
	{
		return InputEventCallback->GetSynchronizationState();
	}
	return ECustomTimeStepSynchronizationState::Closed;
}

FFrameRate UBlackmagicCustomTimeStep::GetFixedFrameRate() const
{
	return MediaConfiguration.MediaMode.FrameRate;
}

void UBlackmagicCustomTimeStep::BeginDestroy()
{
	ReleaseResources();
	Super::BeginDestroy();
}

void UBlackmagicCustomTimeStep::WaitForSync() const
{
	if (InputEventCallback)
	{
		InputEventCallback->WaitForSync();
	}
}

void UBlackmagicCustomTimeStep::ReleaseResources()
{
	if (InputEventCallback)
	{
		InputEventCallback->Uninitialize();
		InputEventCallback = nullptr;
	}
}
