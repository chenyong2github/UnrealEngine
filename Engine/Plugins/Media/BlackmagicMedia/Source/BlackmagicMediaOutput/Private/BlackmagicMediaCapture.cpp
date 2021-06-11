// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlackmagicMediaCapture.h"


#include "BlackmagicLib.h"
#include "BlackmagicMediaOutput.h"
#include "BlackmagicMediaOutputModule.h"
#include "Engine/RendererSettings.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "IBlackmagicMediaModule.h"
#include "MediaIOCoreFileWriter.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "EngineAnalytics.h"
#endif


bool bBlackmagicWritInputRawDataCmdEnable = false;
static FAutoConsoleCommand BlackmagicWriteInputRawDataCmd(
	TEXT("Blackmagic.WriteInputRawData"),
	TEXT("Write Blackmagic raw input buffer to file."),
	FConsoleCommandDelegate::CreateLambda([]() { bBlackmagicWritInputRawDataCmdEnable = true; })
	);

namespace BlackmagicMediaCaptureHelpers
{
	class FBlackmagicMediaCaptureEventCallback : public BlackmagicDesign::IOutputEventCallback
	{
	public:
		FBlackmagicMediaCaptureEventCallback(UBlackmagicMediaCapture* InOwner, const BlackmagicDesign::FChannelInfo& InChannelInfo)
			: RefCounter(0)
			, Owner(InOwner)
			, ChannelInfo(InChannelInfo)
			, LastFramesDroppedCount(0)
		{
		}

		bool Initialize(const BlackmagicDesign::FOutputChannelOptions& InChannelOptions)
		{
			AddRef();

			check(!BlackmagicIdendifier.IsValid());
			BlackmagicDesign::ReferencePtr<BlackmagicDesign::IOutputEventCallback> SelfCallbackRef(this);
			BlackmagicIdendifier = BlackmagicDesign::RegisterOutputChannel(ChannelInfo, InChannelOptions, SelfCallbackRef);
			return BlackmagicIdendifier.IsValid();
		}

		void Uninitialize()
		{
			{
				FScopeLock Lock(&CallbackLock);
				BlackmagicDesign::UnregisterOutputChannel(ChannelInfo, BlackmagicIdendifier, true);
				Owner = nullptr;
			}

			Release();
		}

		bool SendVideoFrameData(BlackmagicDesign::FFrameDescriptor& InFrameDescriptor)
		{
			return BlackmagicDesign::SendVideoFrameData(ChannelInfo, InFrameDescriptor);
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

		virtual void OnInitializationCompleted(bool bSuccess)
		{
			FScopeLock Lock(&CallbackLock);
			if (Owner != nullptr)
			{
				Owner->SetState(bSuccess ? EMediaCaptureState::Capturing : EMediaCaptureState::Error);
			}
		}

		virtual void OnShutdownCompleted() override
		{
			FScopeLock Lock(&CallbackLock);
			if (Owner != nullptr)
			{
				Owner->SetState(EMediaCaptureState::Stopped);
				if (Owner->WakeUpEvent)
				{
					Owner->WakeUpEvent->Trigger();
				}
			}
		}


		virtual void OnOutputFrameCopied(const FFrameSentInfo& InFrameInfo)
		{
			FScopeLock Lock(&CallbackLock);
			if (Owner != nullptr)
			{
				if (Owner->WakeUpEvent)
				{
					Owner->WakeUpEvent->Trigger();
				}

				if (Owner->bLogDropFrame)
				{
					const uint32 FrameDropCount = InFrameInfo.FramesDropped;
					if (FrameDropCount > LastFramesDroppedCount)
					{
						UE_LOG(LogBlackmagicMediaOutput, Warning, TEXT("Lost %d frames on Blackmagic device %d. Frame rate may be too slow."), FrameDropCount - LastFramesDroppedCount, ChannelInfo.DeviceIndex);
					}
					LastFramesDroppedCount = FrameDropCount;
				}
			}
		}

		virtual void OnPlaybackStopped()
		{
			FScopeLock Lock(&CallbackLock);
			if (Owner != nullptr)
			{
				Owner->SetState(EMediaCaptureState::Error);
				if (Owner->WakeUpEvent)
				{
					Owner->WakeUpEvent->Trigger();
				}
			}
		}

		virtual void OnInterlacedOddFieldEvent()
		{
			FScopeLock Lock(&CallbackLock);
			if (Owner != nullptr && Owner->WakeUpEvent)
			{
				Owner->WakeUpEvent->Trigger();
			}
		}


	private:
		TAtomic<int32> RefCounter;
		mutable FCriticalSection CallbackLock;
		UBlackmagicMediaCapture* Owner;

		BlackmagicDesign::FChannelInfo ChannelInfo;
		BlackmagicDesign::FUniqueIdentifier BlackmagicIdendifier;
		uint32 LastFramesDroppedCount;
	};
}



/* namespace BlackmagicMediaCaptureDevice
*****************************************************************************/
namespace BlackmagicMediaCaptureDevice
{
	BlackmagicDesign::FTimecode ConvertToBlackmagicTimecode(const FTimecode& InTimecode, float InEngineFPS, float InBlackmagicFPS)
	{
		const float Divider = InEngineFPS / InBlackmagicFPS;

		BlackmagicDesign::FTimecode Timecode;
		Timecode.Hours = InTimecode.Hours;
		Timecode.Minutes = InTimecode.Minutes;
		Timecode.Seconds = InTimecode.Seconds;
		Timecode.Frames = int32(float(InTimecode.Frames) / Divider);
		Timecode.bIsDropFrame = InTimecode.bDropFrameFormat;
		return Timecode;
	}
}

#if WITH_EDITOR
namespace BlackmagicMediaCaptureAnalytics
{
	/**
	 * @EventName MediaFramework.BlackmagicCaptureStarted
	 * @Trigger Triggered when a Blackmagic capture of the viewport or render target is started.
	 * @Type Client
	 * @Owner MediaIO Team
	 */
	void SendCaptureEvent(const FIntPoint& Resolution, const FFrameRate FrameRate, const FString& CaptureType)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> EventAttributes;
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CaptureType"), CaptureType));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ResolutionWidth"), FString::Printf(TEXT("%d"), Resolution.X)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ResolutionHeight"), FString::Printf(TEXT("%d"), Resolution.Y)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FrameRate"), FrameRate.ToPrettyText().ToString()));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.BlackmagicCaptureStarted"), EventAttributes);
		}
	}
}
#endif


///* UBlackmagicMediaCapture implementation
//*****************************************************************************/
UBlackmagicMediaCapture::UBlackmagicMediaCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bWaitForSyncEvent(false)
	, bEncodeTimecodeInTexel(false)
	, bLogDropFrame(false)
	, BlackmagicMediaOutputPixelFormat(EBlackmagicMediaOutputPixelFormat::PF_8BIT_YUV)
	, bSavedIgnoreTextureAlpha(false)
	, bIgnoreTextureAlphaChanged(false)
	, FrameRate(30, 1)
	, WakeUpEvent(nullptr)
	, LastFrameDropCount_BlackmagicThread(0)
{
}

bool UBlackmagicMediaCapture::ValidateMediaOutput() const
{
	UBlackmagicMediaOutput* BlackmagicMediaOutput = Cast<UBlackmagicMediaOutput>(MediaOutput);
	if (!BlackmagicMediaOutput)
	{
		UE_LOG(LogBlackmagicMediaOutput, Error, TEXT("Can not start the capture. MediaOutput's class is not supported."));
		return false;
	}

	return true;
}

bool UBlackmagicMediaCapture::CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	UBlackmagicMediaOutput* BlackmagicMediaOutput = CastChecked<UBlackmagicMediaOutput>(MediaOutput);
	bool bResult = InitBlackmagic(BlackmagicMediaOutput);
	if (bResult)
	{
		ApplyViewportTextureAlpha(InSceneViewport);
		BlackmagicMediaOutputPixelFormat = BlackmagicMediaOutput->PixelFormat;
#if WITH_EDITOR
		BlackmagicMediaCaptureAnalytics::SendCaptureEvent(BlackmagicMediaOutput->GetRequestedSize(), FrameRate, TEXT("SceneViewport"));
#endif
	}
	return bResult;
}

bool UBlackmagicMediaCapture::CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	UBlackmagicMediaOutput* BlackmagicMediaOutput = CastChecked<UBlackmagicMediaOutput>(MediaOutput);
	bool bResult = InitBlackmagic(BlackmagicMediaOutput);
	BlackmagicMediaOutputPixelFormat = BlackmagicMediaOutput->PixelFormat;
#if WITH_EDITOR
	BlackmagicMediaCaptureAnalytics::SendCaptureEvent(BlackmagicMediaOutput->GetRequestedSize(), FrameRate, TEXT("RenderTarget"));
#endif
	return bResult;
}

bool UBlackmagicMediaCapture::UpdateSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	RestoreViewportTextureAlpha(GetCapturingSceneViewport());
	ApplyViewportTextureAlpha(InSceneViewport);
	return true;
}

bool UBlackmagicMediaCapture::UpdateRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	RestoreViewportTextureAlpha(GetCapturingSceneViewport());
	return true;
}

void UBlackmagicMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	if (!bAllowPendingFrameToBeProcess)
	{
		{
			// Prevent the rendering thread from copying while we are stopping the capture.
			FScopeLock ScopeLock(&RenderThreadCriticalSection);

			if (EventCallback)
			{
				EventCallback->Uninitialize();
				EventCallback = nullptr;
			}

			if (WakeUpEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(WakeUpEvent);
				WakeUpEvent = nullptr;
			}
		}

		RestoreViewportTextureAlpha(GetCapturingSceneViewport());
	}
}

void UBlackmagicMediaCapture::ApplyViewportTextureAlpha(TSharedPtr<FSceneViewport> InSceneViewport)
{
	if (InSceneViewport.IsValid())
	{
		TSharedPtr<SViewport> Widget(InSceneViewport->GetViewportWidget().Pin());
		if (Widget.IsValid())
		{
			bSavedIgnoreTextureAlpha = Widget->GetIgnoreTextureAlpha();

			UBlackmagicMediaOutput* BlackmagicMediaOutput = CastChecked<UBlackmagicMediaOutput>(MediaOutput);
			if (BlackmagicMediaOutput->OutputConfiguration.OutputType == EMediaIOOutputType::FillAndKey)
			{
				if (bSavedIgnoreTextureAlpha)
				{
					bIgnoreTextureAlphaChanged = true;
					Widget->SetIgnoreTextureAlpha(false);
				}
			}
		}
	}
}

void UBlackmagicMediaCapture::RestoreViewportTextureAlpha(TSharedPtr<FSceneViewport> InSceneViewport)
{
	// restore the ignore texture alpha state
	if (bIgnoreTextureAlphaChanged)
	{
		if (InSceneViewport.IsValid())
		{
			TSharedPtr<SViewport> Widget(InSceneViewport->GetViewportWidget().Pin());
			if (Widget.IsValid())
			{
				Widget->SetIgnoreTextureAlpha(bSavedIgnoreTextureAlpha);
			}
		}
		bIgnoreTextureAlphaChanged = false;
	}
}

bool UBlackmagicMediaCapture::HasFinishedProcessing() const
{
	return Super::HasFinishedProcessing() || EventCallback == nullptr;
}

bool UBlackmagicMediaCapture::InitBlackmagic(UBlackmagicMediaOutput* InBlackmagicMediaOutput)
{
	check(InBlackmagicMediaOutput);

	IBlackmagicMediaModule& MediaModule = FModuleManager::LoadModuleChecked<IBlackmagicMediaModule>(TEXT("BlackmagicMedia"));
	if (!MediaModule.CanBeUsed())
	{
		UE_LOG(LogBlackmagicMediaOutput, Warning, TEXT("The BlackmagicMediaCapture can't open MediaOutput '%s' because Blackmagic card cannot be used. Are you in a Commandlet? You may override this behavior by launching with -ForceBlackmagicUsage"), *InBlackmagicMediaOutput->GetName());
		return false;
	}

	// Init general settings
	bWaitForSyncEvent = InBlackmagicMediaOutput->bWaitForSyncEvent;
	bEncodeTimecodeInTexel = InBlackmagicMediaOutput->bEncodeTimecodeInTexel;
	bLogDropFrame = InBlackmagicMediaOutput->bLogDropFrame;
	FrameRate = InBlackmagicMediaOutput->GetRequestedFrameRate();

	// Init Device options
	BlackmagicDesign::FOutputChannelOptions ChannelOptions;
	ChannelOptions.FormatInfo.DisplayMode = InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.DeviceModeIdentifier;
	
	ChannelOptions.FormatInfo.Width = InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.Resolution.X;
	ChannelOptions.FormatInfo.Height = InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.Resolution.Y;
	ChannelOptions.FormatInfo.FrameRateNumerator = InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.FrameRate.Numerator;
	ChannelOptions.FormatInfo.FrameRateDenominator = InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.FrameRate.Denominator;

	switch(InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.Standard)
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

	switch (InBlackmagicMediaOutput->PixelFormat)
	{
	case EBlackmagicMediaOutputPixelFormat::PF_8BIT_YUV:
		ChannelOptions.PixelFormat = BlackmagicDesign::EPixelFormat::pf_8Bits;
		break;
	case EBlackmagicMediaOutputPixelFormat::PF_10BIT_YUV:
	default:
		ChannelOptions.PixelFormat = BlackmagicDesign::EPixelFormat::pf_10Bits;
		break;
	}

	switch (InBlackmagicMediaOutput->TimecodeFormat)
	{
	case EMediaIOTimecodeFormat::LTC:
		ChannelOptions.TimecodeFormat = BlackmagicDesign::ETimecodeFormat::TCF_LTC;
		break;
	case EMediaIOTimecodeFormat::VITC:
		ChannelOptions.TimecodeFormat = BlackmagicDesign::ETimecodeFormat::TCF_VITC1;
		break;
	case EMediaIOTimecodeFormat::None:
	default:
		ChannelOptions.TimecodeFormat = BlackmagicDesign::ETimecodeFormat::TCF_None;
		break;
	}

	switch (InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection.TransportType)
	{
	case EMediaIOTransportType::SingleLink:
	case EMediaIOTransportType::HDMI: // Blackmagic support HDMI but it is not shown in UE4 UI. It's configured in BMD design tool and it's consider a normal link by UE4.
		ChannelOptions.LinkConfiguration = BlackmagicDesign::ELinkConfiguration::SingleLink;
		break;
	case EMediaIOTransportType::DualLink:
		ChannelOptions.LinkConfiguration = BlackmagicDesign::ELinkConfiguration::DualLink;
		break;
	case EMediaIOTransportType::QuadLink:
	default:
		ChannelOptions.LinkConfiguration = BlackmagicDesign::ELinkConfiguration::QuadLinkTSI;
		if (InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection.QuadTransportType == EMediaIOQuadLinkTransportType::SquareDivision)
		{
			ChannelOptions.LinkConfiguration = BlackmagicDesign::ELinkConfiguration::QuadLinkSqr;
		}
		break;
	}

	ChannelOptions.bOutputKey = InBlackmagicMediaOutput->OutputConfiguration.OutputType == EMediaIOOutputType::FillAndKey;
	ChannelOptions.NumberOfBuffers = FMath::Clamp(InBlackmagicMediaOutput->NumberOfBlackmagicBuffers, 3, 4);
	ChannelOptions.bOutputVideo = true;
	ChannelOptions.bOutputInterlacedFieldsTimecodeNeedToMatch = InBlackmagicMediaOutput->bInterlacedFieldsTimecodeNeedToMatch && InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.Standard == EMediaIOStandardType::Interlaced && InBlackmagicMediaOutput->TimecodeFormat != EMediaIOTimecodeFormat::None;
	ChannelOptions.bLogDropFrames = bLogDropFrame;

	check(EventCallback == nullptr);
	BlackmagicDesign::FChannelInfo ChannelInfo;
	ChannelInfo.DeviceIndex = InBlackmagicMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection.Device.DeviceIdentifier;
	EventCallback = new BlackmagicMediaCaptureHelpers::FBlackmagicMediaCaptureEventCallback(this, ChannelInfo);

	bool bSuccess = EventCallback->Initialize(ChannelOptions);
	if (!bSuccess)
	{
		UE_LOG(LogBlackmagicMediaOutput, Warning, TEXT("The Blackmagic output port for '%s' could not be opened."), *InBlackmagicMediaOutput->GetName());
		EventCallback->Uninitialize();
		EventCallback = nullptr;
		return false;
	}

	if (bSuccess && bWaitForSyncEvent)
	{
		const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		bool bLockToVsync = CVar->GetValueOnGameThread() != 0;
		if (bLockToVsync)
		{
			UE_LOG(LogBlackmagicMediaOutput, Warning, TEXT("The Engine use VSync and '%s' wants to wait for the sync event. This may break the \"gen-lock\"."));
		}

		const bool bIsManualReset = false;
		WakeUpEvent = FPlatformProcess::GetSynchEventFromPool(bIsManualReset);
	}

	return true;
}

void UBlackmagicMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height)
{
	// Prevent the rendering thread from copying while we are stopping the capture.
	FScopeLock ScopeLock(&RenderThreadCriticalSection);
	if (EventCallback)
	{
		BlackmagicDesign::FTimecode Timecode = BlackmagicMediaCaptureDevice::ConvertToBlackmagicTimecode(InBaseData.SourceFrameTimecode, InBaseData.SourceFrameTimecodeFramerate.AsDecimal(), FrameRate.AsDecimal());

		if (bEncodeTimecodeInTexel)
		{
			switch (BlackmagicMediaOutputPixelFormat)
			{
			case EBlackmagicMediaOutputPixelFormat::PF_8BIT_YUV:
				if (GetConversionOperation() == EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT)
				{
					FMediaIOCoreEncodeTime EncodeTime(EMediaIOCoreEncodePixelFormat::CharUYVY, InBuffer, Width * 4, Width * 2, Height);
					EncodeTime.Render(Timecode.Hours, Timecode.Minutes, Timecode.Seconds, Timecode.Frames);
					break;
				}
				else
				{
					FMediaIOCoreEncodeTime EncodeTime(EMediaIOCoreEncodePixelFormat::CharBGRA, InBuffer, Width * 4, Width, Height);
					EncodeTime.Render(Timecode.Hours, Timecode.Minutes, Timecode.Seconds, Timecode.Frames);
					break;
				}
			case EBlackmagicMediaOutputPixelFormat::PF_10BIT_YUV:
				FMediaIOCoreEncodeTime EncodeTime(EMediaIOCoreEncodePixelFormat::YUVv210, InBuffer, Width * 16, Width * 6, Height);
				EncodeTime.Render(Timecode.Hours, Timecode.Minutes, Timecode.Seconds, Timecode.Frames);
				break;
			}
		}

		if (bBlackmagicWritInputRawDataCmdEnable)
		{
			FString OutputFilename;
			uint32 Stride = 0;

			switch (BlackmagicMediaOutputPixelFormat)
			{
			case EBlackmagicMediaOutputPixelFormat::PF_8BIT_YUV:
				if (GetConversionOperation() == EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT)
				{
					OutputFilename = TEXT("Blackmagic_Input_8_YUV");
					Stride = Width * 4;
					break;
				}
				else
				{
					OutputFilename = TEXT("Blackmagic_Input_8_RGBA");
					Stride = Width * 4;
					break;
				}
			case EBlackmagicMediaOutputPixelFormat::PF_10BIT_YUV:
				OutputFilename = TEXT("Blackmagic_Input_10_YUV");
				Stride = Width * 16;
				break;
			}

			MediaIOCoreFileWriter::WriteRawFile(OutputFilename, reinterpret_cast<uint8*>(InBuffer), Stride * Height);
			bBlackmagicWritInputRawDataCmdEnable = false;
		}

		BlackmagicDesign::FFrameDescriptor Frame;
		Frame.VideoBuffer = reinterpret_cast<uint8_t*>(InBuffer);
		Frame.VideoWidth = Width;
		Frame.VideoHeight = Height;
		Frame.Timecode = Timecode;
		Frame.FrameIdentifier = InBaseData.SourceFrameNumber;
		const bool bSent = EventCallback->SendVideoFrameData(Frame);
		if (bLogDropFrame && !bSent)
		{
			UE_LOG(LogBlackmagicMediaOutput, Warning, TEXT("Frame couldn't be sent to Blackmagic device. Engine might be running faster than output."));
		}

		WaitForSync_RenderingThread();
	}
	else if (GetState() != EMediaCaptureState::Stopped)
	{
		SetState(EMediaCaptureState::Error);
	}
}

void UBlackmagicMediaCapture::WaitForSync_RenderingThread()
{
	if (bWaitForSyncEvent)
	{
		if (WakeUpEvent && GetState() == EMediaCaptureState::Capturing) // In render thread, could be shutdown in a middle of a frame
		{
			const uint32 NumberOfMilliseconds = 1000;
			if (!WakeUpEvent->Wait(NumberOfMilliseconds))
			{
				SetState(EMediaCaptureState::Error);
				UE_LOG(LogBlackmagicMediaOutput, Error, TEXT("Could not synchronize with the device."));
			}
		}
	}
}
