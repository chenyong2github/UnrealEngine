// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaCapture.h"

#include "IRivermaxCoreModule.h"
#include "IRivermaxOutputStream.h"
#include "RivermaxMediaLog.h"
#include "RivermaxMediaOutput.h"
#include "RivermaxTypes.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "EngineAnalytics.h"
#endif



/* namespace RivermaxMediaCaptureDevice
*****************************************************************************/

#if WITH_EDITOR
namespace RivermaxMediaCaptureAnalytics
{
	/**
	 * @EventName MediaFramework.RivermaxCaptureStarted
	 * @Trigger Triggered when a Rivermax capture of the viewport or render target is started.
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
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.RivermaxCaptureStarted"), EventAttributes);
		}
	}
}
#endif


///* URivermaxMediaCapture implementation
//*****************************************************************************/

bool URivermaxMediaCapture::ValidateMediaOutput() const
{
	URivermaxMediaOutput* RivermaxMediaOutput = Cast<URivermaxMediaOutput>(MediaOutput);
	if (!RivermaxMediaOutput)
	{
		UE_LOG(LogRivermaxMedia, Error, TEXT("Can not start the capture. MediaOutput's class is not supported."));
		return false;
	}

	return true;
}

bool URivermaxMediaCapture::CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	URivermaxMediaOutput* RivermaxOutput = CastChecked<URivermaxMediaOutput>(MediaOutput);
	const bool bResult = Initialize(RivermaxOutput);
	if (bResult)
	{
#if WITH_EDITOR
		RivermaxMediaCaptureAnalytics::SendCaptureEvent(RivermaxOutput->GetRequestedSize(), RivermaxOutput->FrameRate, TEXT("SceneViewport"));
#endif
	}
	return bResult;
}

bool URivermaxMediaCapture::CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	URivermaxMediaOutput* RivermaxOutput = CastChecked<URivermaxMediaOutput>(MediaOutput);
	bool bResult = Initialize(RivermaxOutput);
#if WITH_EDITOR
	if (bResult)
	{
		RivermaxMediaCaptureAnalytics::SendCaptureEvent(RivermaxOutput->GetRequestedSize(), RivermaxOutput->FrameRate, TEXT("RenderTarget"));
	}
#endif
	return bResult;
}

bool URivermaxMediaCapture::UpdateSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	return true;
}

bool URivermaxMediaCapture::UpdateRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	return true;
}

void URivermaxMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	if (RivermaxStream)
	{
		RivermaxStream->Uninitialize();
		RivermaxStream.Reset();
	}
}

bool URivermaxMediaCapture::ShouldCaptureRHITexture() const
{
	//todo for gpudirect support
	return false;  
}

void URivermaxMediaCapture::BeforeFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FTextureRHIRef InTexture)
{
	if (ShouldCaptureRHITexture())
	{
	
	}
}

bool URivermaxMediaCapture::HasFinishedProcessing() const
{
	return Super::HasFinishedProcessing();
}

bool URivermaxMediaCapture::Initialize(URivermaxMediaOutput* InMediaOutput)
{
	using namespace UE::RivermaxCore;

	check(InMediaOutput);

	IRivermaxCoreModule* Module = FModuleManager::GetModulePtr<IRivermaxCoreModule>("RivermaxCore");
	if (Module)
	{
		FRivermaxStreamOptions Options;
		if (ConfigureStream(InMediaOutput, Options))
		{
			RivermaxStream = Module->CreateOutputStream();
			if (RivermaxStream)
			{
				return RivermaxStream->Initialize(Options, *this);
			}
		}
	}

	return false;
}

bool URivermaxMediaCapture::ConfigureStream(URivermaxMediaOutput* InMediaOutput, UE::RivermaxCore::FRivermaxStreamOptions& OutOptions) const
{
	using namespace UE::RivermaxCore;

	OutOptions.DestinationAddress = InMediaOutput->DestinationAddress;
	OutOptions.SourceAddress = InMediaOutput->SourceAddress;
	OutOptions.Port = InMediaOutput->Port;
	OutOptions.Resolution = InMediaOutput->Resolution;
	OutOptions.FrameRate = InMediaOutput->FrameRate;
	OutOptions.NumberOfBuffers = InMediaOutput->NumberOfTextureBuffers;

	switch (InMediaOutput->PixelFormat)
	{
	case ERivermaxMediaOutputPixelFormat::PF_8BIT_YUV:
	{
		OutOptions.PixelFormat = ERivermaxOutputPixelFormat::RMAX_8BIT_YCBCR;
		break;
	}
	default:
	{
		UE_LOG(LogRivermaxMedia, Error, TEXT("Desired pixel format (%s) is not a valid Rivermax pixel format"), *StaticEnum<ERivermaxMediaOutputPixelFormat>()->GetValueAsString(InMediaOutput->PixelFormat));
		return false;
	}
	}

	return true;
}

void URivermaxMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow)
{
	using namespace UE::RivermaxCore;

	TRACE_CPUPROFILER_EVENT_SCOPE(URivermaxMediaCapture::OnFrameCaptured_RenderingThread);
	
	FRivermaxOutputVideoFrameInfo NewFrame;
	NewFrame.Height = Height;
	NewFrame.Width = Width;
	NewFrame.Stride = BytesPerRow;
	NewFrame.VideoBuffer = InBuffer;
	NewFrame.FrameIdentifier = InBaseData.SourceFrameNumber;
	RivermaxStream->PushVideoFrame(NewFrame);
}

void URivermaxMediaCapture::OnRHITextureCaptured_RenderingThread(const FCaptureBaseData& InBaseData,	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FTextureRHIRef InTexture)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URivermaxMediaCapture::OnFrameCaptured_RenderingThread);
}

void URivermaxMediaCapture::OnInitializationCompleted(bool bHasSucceed)
{
	if (GetState() != EMediaCaptureState::Stopped)
	{
		SetState(bHasSucceed ? EMediaCaptureState::Capturing : EMediaCaptureState::Error);
	}
}

void URivermaxMediaCapture::OnStreamError()
{
	UE_LOG(LogRivermaxMedia, Error, TEXT("Outputstream has caught an error. Stopping capture."));
	if (GetState() != EMediaCaptureState::Stopped)
	{
		SetState(EMediaCaptureState::Error);
	}
}
