// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlackmagicTimecodeProvider.h"
#include "BlackmagicMediaPrivate.h"
#include "Blackmagic.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Templates/Atomic.h"


namespace BlackmagicTimecodeProviderHelpers
{
	//~ FEventCallback implementation
	//--------------------------------------------------------------------
	class FEventCallback : public BlackmagicDesign::IInputEventCallback
	{
	public:
		FEventCallback(const BlackmagicDesign::FChannelInfo& InChannelInfo, const FFrameRate& InFrameRate)
			: RefCounter(0)
			, ChannelInfo(InChannelInfo)
			, State(ETimecodeProviderSynchronizationState::Closed)
			, FrameRate(InFrameRate)
			, bHasWarnedMissingTimecode(false)
		{
		}

		bool Initialize(const BlackmagicDesign::FInputChannelOptions& InChannelInfo)
		{
			AddRef();

			BlackmagicDesign::ReferencePtr<BlackmagicDesign::IInputEventCallback> SelfRef(this);
			BlackmagicIdendifier = BlackmagicDesign::RegisterCallbackForChannel(ChannelInfo, InChannelInfo, SelfRef);
			State = BlackmagicIdendifier.IsValid() ? ETimecodeProviderSynchronizationState::Synchronizing : ETimecodeProviderSynchronizationState::Error;
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

		ETimecodeProviderSynchronizationState GetSynchronizationState() const { return State; }

		FTimecode GetTimecode() const
		{
			FScopeLock Lock(&CallbackLock);
			return Timecode;
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
			State = bSuccess ? ETimecodeProviderSynchronizationState::Synchronized : ETimecodeProviderSynchronizationState::Error;
		}

		virtual void OnShutdownCompleted() override
		{
			State = ETimecodeProviderSynchronizationState::Closed;
		}

		virtual void OnFrameReceived(const BlackmagicDesign::IInputEventCallback::FFrameReceivedInfo& InFrameInfo) override
		{
			FScopeLock Lock(&CallbackLock);
			if (InFrameInfo.bHaveTimecode)
			{
				//We expect the timecode to be processed in the library. What we receive will be a "linear" timecode even for frame rates greater than 30.
				if ((int32)InFrameInfo.Timecode.Frames >= FMath::RoundToInt(FrameRate.AsDecimal()))
				{
					UE_LOG(LogBlackmagicMedia, Warning, TEXT("BlackmagicTimecodeProvider input '%d' received an invalid Timecode frame number (%d) for the current frame rate (%s)."), ChannelInfo.DeviceIndex, InFrameInfo.Timecode.Frames, *FrameRate.ToPrettyText().ToString());
				}

				Timecode = FTimecode(InFrameInfo.Timecode.Hours, InFrameInfo.Timecode.Minutes, InFrameInfo.Timecode.Seconds, InFrameInfo.Timecode.Frames, InFrameInfo.Timecode.bIsDropFrame);
			}
			else if (!bHasWarnedMissingTimecode)
			{
				bHasWarnedMissingTimecode = true;
				UE_LOG(LogBlackmagicMedia, Warning, TEXT("BlackmagicTimecodeProvider input '%d' didn't receive any timecode in the last frame. Is your source configured correctly?"), ChannelInfo.DeviceIndex);
			}
		}

		virtual void OnFrameFormatChanged(const BlackmagicDesign::FFormatInfo& NewFormat) override
		{
			UE_LOG(LogBlackmagicMedia, Error, TEXT("The video format changed."));
			State = ETimecodeProviderSynchronizationState::Error;
		}

		virtual void OnInterlacedOddFieldEvent() override
		{
			FScopeLock Lock(&CallbackLock);
			Timecode.Frames++;
		}

	private:
		TAtomic<int32> RefCounter;

		BlackmagicDesign::FUniqueIdentifier BlackmagicIdendifier;
		BlackmagicDesign::FChannelInfo ChannelInfo;

		mutable FCriticalSection CallbackLock;
		FTimecode Timecode;

		ETimecodeProviderSynchronizationState State;

		FFrameRate FrameRate;

		bool bHasWarnedMissingTimecode;
	};

}


//~ UBlackmagicTimecodeProvider implementation
//--------------------------------------------------------------------
UBlackmagicTimecodeProvider::UBlackmagicTimecodeProvider(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EventCallback(nullptr)
{
	MediaConfiguration.bIsInput = true;
}

bool UBlackmagicTimecodeProvider::FetchTimecode(FQualifiedFrameTime& OutFrameTime)
{
	if (!EventCallback || (EventCallback->GetSynchronizationState() != ETimecodeProviderSynchronizationState::Synchronized))
	{
		return false;
	}

	const FFrameRate Rate = MediaConfiguration.MediaMode.FrameRate;
	const FTimecode Timecode = EventCallback->GetTimecode();

	OutFrameTime = FQualifiedFrameTime(Timecode, Rate);

	return true;
}

ETimecodeProviderSynchronizationState UBlackmagicTimecodeProvider::GetSynchronizationState() const
{
	return EventCallback ? EventCallback->GetSynchronizationState() : ETimecodeProviderSynchronizationState::Closed;
}

bool UBlackmagicTimecodeProvider::Initialize(class UEngine* InEngine)
{
	if (!MediaConfiguration.IsValid())
	{
		UE_LOG(LogBlackmagicMedia, Warning, TEXT("The configuration of '%s' is not valid."), *GetName());
		return false;
	}

	if (!FBlackmagic::IsInitialized())
	{
		UE_LOG(LogBlackmagicMedia, Error, TEXT("The TimecodeProvider '%s' can't be initialized. Blackmagic is not initialized on your machine."), *GetName());
		return false;
	}

	if (!FBlackmagic::CanUseBlackmagicCard())
	{
		UE_LOG(LogBlackmagicMedia, Warning, TEXT("The TimecodeProvider '%s' can't be initialized because Blackmagic card cannot be used. Are you in a Commandlet? You may override this behavior by launching with -ForceBlackmagicUsage"), *GetName());
		return false;
	}

	if (TimecodeFormat == EMediaIOTimecodeFormat::None)
	{
		UE_LOG(LogBlackmagicMedia, Error, TEXT("The TimecodeProvider '%s' can't be initialized. Selected timecode format is invalid."), *GetName());
		return false;
	}

	BlackmagicDesign::FChannelInfo ChannelInfo;
	ChannelInfo.DeviceIndex = MediaConfiguration.MediaConnection.Device.DeviceIdentifier;

	check(EventCallback == nullptr);
	EventCallback = new BlackmagicTimecodeProviderHelpers::FEventCallback(ChannelInfo, GetFrameRate());

	BlackmagicDesign::FInputChannelOptions ChannelOptions;
	ChannelOptions.CallbackPriority = 5;
	ChannelOptions.FormatInfo.DisplayMode = MediaConfiguration.MediaMode.DeviceModeIdentifier;
	ChannelOptions.FormatInfo.FrameRateNumerator = MediaConfiguration.MediaMode.FrameRate.Numerator;
	ChannelOptions.FormatInfo.FrameRateDenominator = MediaConfiguration.MediaMode.FrameRate.Denominator;
	switch (MediaConfiguration.MediaMode.Standard)
	{
	case EMediaIOStandardType::Interlaced:
		ChannelOptions.FormatInfo.FieldDominance = BlackmagicDesign::EFieldDominance::Interlaced;
		break;
	case EMediaIOStandardType::ProgressiveSegmentedFrame:
		ChannelOptions.FormatInfo.FieldDominance = BlackmagicDesign::EFieldDominance::ProgressiveSegmentedFrame;
		break;
	case EMediaIOStandardType::Progressive:
	default:
		ChannelOptions.FormatInfo.FieldDominance = BlackmagicDesign::EFieldDominance::Progressive;
		break;
	}

	ChannelOptions.TimecodeFormat = BlackmagicDesign::ETimecodeFormat::TCF_None;
	switch (TimecodeFormat)
	{
	case EMediaIOTimecodeFormat::LTC:
		ChannelOptions.TimecodeFormat = BlackmagicDesign::ETimecodeFormat::TCF_LTC;
		break;
	case EMediaIOTimecodeFormat::VITC:
		ChannelOptions.TimecodeFormat = BlackmagicDesign::ETimecodeFormat::TCF_VITC1;
		break;
	default:
		break;
	}


	const bool bSuccess = EventCallback->Initialize(ChannelOptions);
	if (!bSuccess)
	{
		ReleaseResources();
	}

	return bSuccess;
}

void UBlackmagicTimecodeProvider::Shutdown(class UEngine* InEngine)
{
	ReleaseResources();
}

void UBlackmagicTimecodeProvider::BeginDestroy()
{
	ReleaseResources();
	Super::BeginDestroy();
}

void UBlackmagicTimecodeProvider::ReleaseResources()
{
	if (EventCallback)
	{
		EventCallback->Uninitialize();
		EventCallback = nullptr;
	}
}
