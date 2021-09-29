// Copyright Epic Games, Inc. All Rights Reserved.

#include "AjaMediaCapture.h"

#include "AJALib.h"
#include "AjaDeviceProvider.h"
#include "AjaMediaOutput.h"
#include "Engine/RendererSettings.h"
#include "HAL/Event.h"
#include "HAL/IConsoleManager.h"
#include "IAjaMediaModule.h"
#include "IAjaMediaOutputModule.h"
#include "MediaIOCoreFileWriter.h"
#include "Misc/ScopeLock.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "EngineAnalytics.h"
#endif

/* namespace AjaMediaCaptureDevice
*****************************************************************************/
namespace AjaMediaCaptureDevice
{
	AJA::FTimecode ConvertToAJATimecode(const FTimecode& InTimecode, float InEngineFPS, float InAjaFPS)
	{
		const float Divider = InEngineFPS / InAjaFPS;

		AJA::FTimecode Timecode;
		Timecode.Hours = InTimecode.Hours;
		Timecode.Minutes = InTimecode.Minutes;
		Timecode.Seconds = InTimecode.Seconds;
		Timecode.Frames = int32(float(InTimecode.Frames) / Divider);
		return Timecode;
	}
}

#if WITH_EDITOR
namespace AjaMediaCaptureAnalytics
{
	/**
	 * @EventName MediaFramework.AjaCaptureStarted
	 * @Trigger Triggered when a Aja capture of the viewport or render target is started.
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
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.AjaCaptureStarted"), EventAttributes);
		}
	}
}
#endif

bool bAjaWritInputRawDataCmdEnable = false;
static FAutoConsoleCommand AjaWriteInputRawDataCmd(
	TEXT("Aja.WriteInputRawData"),
	TEXT("Write Aja raw input buffer to file."),
	FConsoleCommandDelegate::CreateLambda([]() { bAjaWritInputRawDataCmdEnable = true; })
	);

///* FAjaOutputCallback definition
//*****************************************************************************/
struct UAjaMediaCapture::FAjaOutputCallback : public AJA::IAJAInputOutputChannelCallbackInterface
{
	virtual void OnInitializationCompleted(bool bSucceed) override;
	virtual bool OnRequestInputBuffer(const AJA::AJARequestInputBufferData& RequestBuffer, AJA::AJARequestedInputBufferData& OutRequestedBuffer) override;
	virtual bool OnInputFrameReceived(const AJA::AJAInputFrameData& InInputFrame, const AJA::AJAAncillaryFrameData& InAncillaryFrame, const AJA::AJAAudioFrameData& AudioFrame, const AJA::AJAVideoFrameData& VideoFrame) override;
	virtual bool OnOutputFrameCopied(const AJA::AJAOutputFrameData& InFrameData) override;
	virtual void OnOutputFrameStarted() override;
	virtual void OnCompletion(bool bSucceed) override;
	UAjaMediaCapture* Owner;

	/** Last frame drop count to detect count */
	uint64 LastFrameDropCount = 0;
	uint64 PreviousDroppedCount = 0;
};

///* FAjaOutputCallback definition
//*****************************************************************************/
struct UAjaMediaCapture::FAJAOutputChannel : public AJA::AJAOutputChannel
{
	FAJAOutputChannel() = default;
};

///* UAjaMediaCapture implementation
//*****************************************************************************/
UAjaMediaCapture::UAjaMediaCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, OutputChannel(nullptr)
	, OutputCallback(nullptr)
	, bWaitForSyncEvent(false)
	, bLogDropFrame(false)
	, bEncodeTimecodeInTexel(false)
	, PixelFormat(EAjaMediaOutputPixelFormat::PF_8BIT_YUV)
	, UseKey(false)
	, bSavedIgnoreTextureAlpha(false)
	, bIgnoreTextureAlphaChanged(false)
	, FrameRate(30, 1)
	, WakeUpEvent(nullptr)
{
}

bool UAjaMediaCapture::ValidateMediaOutput() const
{
	UAjaMediaOutput* AjaMediaOutput = Cast<UAjaMediaOutput>(MediaOutput);
	if (!AjaMediaOutput)
	{
		UE_LOG(LogAjaMediaOutput, Error, TEXT("Can not start the capture. MediaSource's class is not supported."));
		return false;
	}

	return true;
}

bool UAjaMediaCapture::CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	UAjaMediaOutput* AjaMediaSource = CastChecked<UAjaMediaOutput>(MediaOutput);
	bool bResult = InitAJA(AjaMediaSource);
	if (bResult)
	{
		ApplyViewportTextureAlpha(InSceneViewport);
#if WITH_EDITOR
		AjaMediaCaptureAnalytics::SendCaptureEvent(AjaMediaSource->GetRequestedSize(), FrameRate, TEXT("SceneViewport"));
#endif
	}
	return bResult;
}

bool UAjaMediaCapture::CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	UAjaMediaOutput* AjaMediaSource = CastChecked<UAjaMediaOutput>(MediaOutput);
	bool bResult = InitAJA(AjaMediaSource);
#if WITH_EDITOR
	if (bResult)
	{
		AjaMediaCaptureAnalytics::SendCaptureEvent(AjaMediaSource->GetRequestedSize(), FrameRate, TEXT("RenderTarget"));
	}
#endif
	return bResult;
}

bool UAjaMediaCapture::UpdateSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	RestoreViewportTextureAlpha(GetCapturingSceneViewport());
	ApplyViewportTextureAlpha(InSceneViewport);
	return true;
}

bool UAjaMediaCapture::UpdateRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	RestoreViewportTextureAlpha(GetCapturingSceneViewport());
	return true;
}

void UAjaMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	if (!bAllowPendingFrameToBeProcess)
	{
		{
			// Prevent the rendering thread from copying while we are stopping the capture.
			FScopeLock ScopeLock(&RenderThreadCriticalSection);

			if (OutputChannel)
			{
				// Close the aja channel in the another thread.
				OutputChannel->Uninitialize();
				delete OutputChannel;
				OutputChannel = nullptr;
				delete OutputCallback;
				OutputCallback = nullptr;
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

void UAjaMediaCapture::ApplyViewportTextureAlpha(TSharedPtr<FSceneViewport> InSceneViewport)
{
	if (InSceneViewport.IsValid())
	{
		TSharedPtr<SViewport> Widget(InSceneViewport->GetViewportWidget().Pin());
		if (Widget.IsValid())
		{
			bSavedIgnoreTextureAlpha = Widget->GetIgnoreTextureAlpha();

			UAjaMediaOutput* AjaMediaSource = CastChecked<UAjaMediaOutput>(MediaOutput);
			if (AjaMediaSource->OutputConfiguration.OutputType == EMediaIOOutputType::FillAndKey)
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

void UAjaMediaCapture::RestoreViewportTextureAlpha(TSharedPtr<FSceneViewport> InSceneViewport)
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

bool UAjaMediaCapture::HasFinishedProcessing() const
{
	return Super::HasFinishedProcessing() || OutputChannel == nullptr;
}

bool UAjaMediaCapture::InitAJA(UAjaMediaOutput* InAjaMediaOutput)
{
	check(InAjaMediaOutput);

	IAjaMediaModule& MediaModule = FModuleManager::LoadModuleChecked<IAjaMediaModule>(TEXT("AjaMedia"));
	if (!MediaModule.CanBeUsed())
	{
		UE_LOG(LogAjaMediaOutput, Warning, TEXT("The AjaMediaCapture can't open MediaOutput '%s' because Aja card cannot be used. Are you in a Commandlet? You may override this behavior by launching with -ForceAjaUsage"), *InAjaMediaOutput->GetName());
		return false;
	}

	// Init general settings
	bWaitForSyncEvent = InAjaMediaOutput->bWaitForSyncEvent;
	bLogDropFrame = InAjaMediaOutput->bLogDropFrame;
	bEncodeTimecodeInTexel = InAjaMediaOutput->bEncodeTimecodeInTexel;
	FrameRate = InAjaMediaOutput->GetRequestedFrameRate();
	PortName = FAjaDeviceProvider().ToText(InAjaMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection).ToString();

	// Init Device options
	AJA::AJADeviceOptions DeviceOptions(InAjaMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection.Device.DeviceIdentifier);

	OutputCallback = new UAjaMediaCapture::FAjaOutputCallback();
	OutputCallback->Owner = this;

	AJA::AJAVideoFormats::VideoFormatDescriptor Descriptor = AJA::AJAVideoFormats::GetVideoFormat(InAjaMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.DeviceModeIdentifier);

	// Init Channel options
	AJA::AJAInputOutputChannelOptions ChannelOptions(TEXT("ViewportOutput"), InAjaMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection.PortIdentifier);
	ChannelOptions.CallbackInterface = OutputCallback;
	ChannelOptions.bOutput = true;
	ChannelOptions.NumberOfAudioChannel = 0;
	ChannelOptions.SynchronizeChannelIndex = InAjaMediaOutput->OutputConfiguration.ReferencePortIdentifier;
	ChannelOptions.KeyChannelIndex = InAjaMediaOutput->OutputConfiguration.KeyPortIdentifier;
	ChannelOptions.OutputNumberOfBuffers = InAjaMediaOutput->NumberOfAJABuffers;
	ChannelOptions.VideoFormatIndex = InAjaMediaOutput->OutputConfiguration.MediaConfiguration.MediaMode.DeviceModeIdentifier;
	ChannelOptions.bUseAutoCirculating = InAjaMediaOutput->bOutputWithAutoCirculating;
	ChannelOptions.bUseKey = InAjaMediaOutput->OutputConfiguration.OutputType == EMediaIOOutputType::FillAndKey;  // must be RGBA to support Fill+Key
	ChannelOptions.bUseAncillary = false;
	ChannelOptions.bUseAudio = false;
	ChannelOptions.bUseVideo = true;
	ChannelOptions.bOutputInterlacedFieldsTimecodeNeedToMatch = InAjaMediaOutput->bInterlacedFieldsTimecodeNeedToMatch && Descriptor.bIsInterlacedStandard && InAjaMediaOutput->TimecodeFormat != EMediaIOTimecodeFormat::None;
	ChannelOptions.bDisplayWarningIfDropFrames = bLogDropFrame;
	ChannelOptions.bConvertOutputLevelAToB = InAjaMediaOutput->bOutputIn3GLevelB && Descriptor.bIsVideoFormatA;

	{
		const EMediaIOTransportType TransportType = InAjaMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection.TransportType;
		const EMediaIOQuadLinkTransportType QuadTransportType = InAjaMediaOutput->OutputConfiguration.MediaConfiguration.MediaConnection.QuadTransportType;
		switch (TransportType)
		{
		case EMediaIOTransportType::SingleLink:
			ChannelOptions.TransportType = AJA::ETransportType::TT_SdiSingle;
			break;
		case EMediaIOTransportType::DualLink:
			ChannelOptions.TransportType = AJA::ETransportType::TT_SdiDual;
			break;
		case EMediaIOTransportType::QuadLink:
			ChannelOptions.TransportType = QuadTransportType == EMediaIOQuadLinkTransportType::SquareDivision ? AJA::ETransportType::TT_SdiQuadSQ : AJA::ETransportType::TT_SdiQuadTSI;
			break;
		case EMediaIOTransportType::HDMI:
			ChannelOptions.TransportType = AJA::ETransportType::TT_Hdmi;
			break;
		}
	}

	switch (InAjaMediaOutput->PixelFormat)
	{
	case EAjaMediaOutputPixelFormat::PF_8BIT_YUV:
		if (ChannelOptions.bUseKey)
		{
			ChannelOptions.PixelFormat = AJA::EPixelFormat::PF_8BIT_ARGB;
		}
		else
		{
			ChannelOptions.PixelFormat = AJA::EPixelFormat::PF_8BIT_YCBCR;
		}
		break;
	case EAjaMediaOutputPixelFormat::PF_10BIT_YUV:
		if (ChannelOptions.bUseKey)
		{
			ChannelOptions.PixelFormat = AJA::EPixelFormat::PF_10BIT_RGB;
		}
		else
		{
			ChannelOptions.PixelFormat = AJA::EPixelFormat::PF_10BIT_YCBCR;
		}
		break;
	default:
		ChannelOptions.PixelFormat = AJA::EPixelFormat::PF_8BIT_YCBCR;
		break;
	}
	PixelFormat = InAjaMediaOutput->PixelFormat;
	UseKey = ChannelOptions.bUseKey;

	switch (InAjaMediaOutput->TimecodeFormat)
	{
	case EMediaIOTimecodeFormat::None:
		ChannelOptions.TimecodeFormat = AJA::ETimecodeFormat::TCF_None;
		break;
	case EMediaIOTimecodeFormat::LTC:
		ChannelOptions.TimecodeFormat = AJA::ETimecodeFormat::TCF_LTC;
		break;
	case EMediaIOTimecodeFormat::VITC:
		ChannelOptions.TimecodeFormat = AJA::ETimecodeFormat::TCF_VITC1;
		break;
	default:
		ChannelOptions.TimecodeFormat = AJA::ETimecodeFormat::TCF_None;
		break;
	}

	switch(InAjaMediaOutput->OutputConfiguration.OutputReference)
	{
	case EMediaIOReferenceType::External:
		ChannelOptions.OutputReferenceType = AJA::EAJAReferenceType::EAJA_REFERENCETYPE_EXTERNAL;
		break;
	case EMediaIOReferenceType::Input:
		ChannelOptions.OutputReferenceType = AJA::EAJAReferenceType::EAJA_REFERENCETYPE_INPUT;
		break;
	default:
		ChannelOptions.OutputReferenceType = AJA::EAJAReferenceType::EAJA_REFERENCETYPE_FREERUN;
		break;
	}

	OutputChannel = new FAJAOutputChannel();
	if (!OutputChannel->Initialize(DeviceOptions, ChannelOptions))
	{
		UE_LOG(LogAjaMediaOutput, Warning, TEXT("The AJA output port for '%s' could not be opened."), *InAjaMediaOutput->GetName());
		delete OutputChannel;
		OutputChannel = nullptr;
		delete OutputCallback;
		OutputCallback = nullptr;
		return false;
	}

	if (bWaitForSyncEvent)
	{
		const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VSync"));
		bool bLockToVsync = CVar->GetValueOnGameThread() != 0;
		if (bLockToVsync)
		{
			UE_LOG(LogAjaMediaOutput, Warning, TEXT("The Engine use VSync and '%s' wants to wait for the sync event. This may break the \"gen-lock\"."));
		}

		const bool bIsManualReset = false;
		WakeUpEvent = FPlatformProcess::GetSynchEventFromPool(bIsManualReset);
	}

	return true;
}

void UAjaMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height)
{
	// Prevent the rendering thread from copying while we are stopping the capture.
	FScopeLock ScopeLock(&RenderThreadCriticalSection);
	if (OutputChannel)
	{
		AJA::FTimecode Timecode = AjaMediaCaptureDevice::ConvertToAJATimecode(InBaseData.SourceFrameTimecode, InBaseData.SourceFrameTimecodeFramerate.AsDecimal(), FrameRate.AsDecimal());

		uint32 Stride = Width * 4;
		uint32 TimeEncodeWidth = Width;
		EMediaIOCoreEncodePixelFormat EncodePixelFormat = EMediaIOCoreEncodePixelFormat::CharBGRA;
		FString OutputFilename;

		switch (PixelFormat)
		{
		case EAjaMediaOutputPixelFormat::PF_8BIT_YUV:
			if (UseKey)
			{
				Stride = Width * 4;
				TimeEncodeWidth = Width;
				EncodePixelFormat = EMediaIOCoreEncodePixelFormat::CharBGRA;
				OutputFilename = TEXT("Aja_Input_8_RGBA");
				break;
			}
			else
			{
				Stride = Width * 4;
				TimeEncodeWidth = Width * 2;
				EncodePixelFormat = EMediaIOCoreEncodePixelFormat::CharUYVY;
				OutputFilename = TEXT("Aja_Input_8_YUV");
				break;
			}
		case EAjaMediaOutputPixelFormat::PF_10BIT_YUV:
			if (UseKey)
			{
				Stride = Width * 4;
				TimeEncodeWidth = Width;
				EncodePixelFormat = EMediaIOCoreEncodePixelFormat::A2B10G10R10;
				OutputFilename = TEXT("Aja_Input_10_RGBA");
				break;
			}
			else
			{
				Stride = Width * 16;
				TimeEncodeWidth = Width * 6;
				EncodePixelFormat = EMediaIOCoreEncodePixelFormat::YUVv210;
				OutputFilename = TEXT("Aja_Input_10_YUV");
				break;
			}
		}

		if (bEncodeTimecodeInTexel)
		{
			FMediaIOCoreEncodeTime EncodeTime(EncodePixelFormat, InBuffer, Stride, TimeEncodeWidth, Height);
			EncodeTime.Render(Timecode.Hours, Timecode.Minutes, Timecode.Seconds, Timecode.Frames);
		}

		AJA::AJAOutputFrameBufferData FrameBuffer;
		FrameBuffer.Timecode = Timecode;
		FrameBuffer.FrameIdentifier = InBaseData.SourceFrameNumber;
		OutputChannel->SetVideoFrameData(FrameBuffer, reinterpret_cast<uint8_t*>(InBuffer), Stride * Height);

		if (bAjaWritInputRawDataCmdEnable)
		{
			MediaIOCoreFileWriter::WriteRawFile(OutputFilename, reinterpret_cast<uint8*>(InBuffer), Stride * Height);
			bAjaWritInputRawDataCmdEnable = false;
		}

		WaitForSync_RenderingThread();
	}
	else if (GetState() != EMediaCaptureState::Stopped)
	{
		SetState(EMediaCaptureState::Error);
	}
}

void UAjaMediaCapture::WaitForSync_RenderingThread() const
{
	if (bWaitForSyncEvent)
	{
		if (WakeUpEvent && GetState() != EMediaCaptureState::Error) // In render thread, could be shutdown in a middle of a frame
		{
			WakeUpEvent->Wait();
		}
	}
}

/* namespace IAJAInputCallbackInterface implementation
// This is called from the AJA thread. There's a lock inside AJA to prevent this object from dying while in this thread.
*****************************************************************************/
void UAjaMediaCapture::FAjaOutputCallback::OnInitializationCompleted(bool bSucceed)
{
	check(Owner);
	if (Owner->GetState() != EMediaCaptureState::Stopped)
	{
		Owner->SetState(bSucceed ? EMediaCaptureState::Capturing : EMediaCaptureState::Error);
	}

	if (Owner->WakeUpEvent)
	{
		Owner->WakeUpEvent->Trigger();
	}
}

bool UAjaMediaCapture::FAjaOutputCallback::OnOutputFrameCopied(const AJA::AJAOutputFrameData& InFrameData)
{
	const uint32 FrameDropCount = InFrameData.FramesDropped;
	if (Owner->bLogDropFrame)
	{
		if (FrameDropCount > LastFrameDropCount)
		{
			PreviousDroppedCount += FrameDropCount - LastFrameDropCount;

			static const int32 NumMaxFrameBeforeWarning = 50;
			if (PreviousDroppedCount % NumMaxFrameBeforeWarning == 0)
			{
				UE_LOG(LogAjaMediaOutput, Warning, TEXT("Loosing frames on AJA output %s. The current count is %d."), *Owner->PortName, PreviousDroppedCount);
			}
		}
		else if (PreviousDroppedCount > 0)
		{
			UE_LOG(LogAjaMediaOutput, Warning, TEXT("Lost %d frames on AJA output %s. Frame rate may be too slow."), PreviousDroppedCount, *Owner->PortName);
			PreviousDroppedCount = 0;
		}
	}
	LastFrameDropCount = FrameDropCount;

	return true;
}

void UAjaMediaCapture::FAjaOutputCallback::OnOutputFrameStarted()
{
	if (Owner->WakeUpEvent)
	{
		Owner->WakeUpEvent->Trigger();
	}
}

void UAjaMediaCapture::FAjaOutputCallback::OnCompletion(bool bSucceed)
{
	if (!bSucceed)
	{
		Owner->SetState(EMediaCaptureState::Error);
	}

	if (Owner->WakeUpEvent)
	{
		Owner->WakeUpEvent->Trigger();
	}
}

bool UAjaMediaCapture::FAjaOutputCallback::OnRequestInputBuffer(const AJA::AJARequestInputBufferData& RequestBuffer, AJA::AJARequestedInputBufferData& OutRequestedBuffer)
{
	check(false);
	return false;
}

bool UAjaMediaCapture::FAjaOutputCallback::OnInputFrameReceived(const AJA::AJAInputFrameData& InInputFrame, const AJA::AJAAncillaryFrameData& InAncillaryFrame, const AJA::AJAAudioFrameData& AudioFrame, const AJA::AJAVideoFrameData& VideoFrame)
{
	check(false);
	return false;
}
