// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaCapture.h"


#include "Application/ThrottleManager.h"
#include "Async/Async.h"
#include "Engine/GameEngine.h"
#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineModule.h"
#include "MediaIOCoreModule.h"
#include "MediaOutput.h"
#include "MediaShaders.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "PipelineStateCache.h"
#include "RendererInterface.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "Slate/SceneViewport.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "RenderTargetPool.h"

#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "Editor.h"
#include "EngineAnalytics.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "MediaCapture"

/* namespace MediaCaptureDetails definition
*****************************************************************************/

/** Time spent in media capture sending a frame. */
DECLARE_CYCLE_STAT(TEXT("MediaCapture RenderThread CopyToResolve"), STAT_MediaCapture_RenderThread_CopyToResolve, STATGROUP_Media);
DECLARE_CYCLE_STAT(TEXT("MediaCapture RenderThread MapStaging"), STAT_MediaCapture_RenderThread_MapStaging, STATGROUP_Media);
DECLARE_CYCLE_STAT(TEXT("MediaCapture RenderThread Callback"), STAT_MediaCapture_RenderThread_Callback, STATGROUP_Media);

/** These pixel formats do not require additional conversion except for swizzling and normalized sampling. */
static TSet<EPixelFormat> SupportedRgbaSwizzleFormats =
{
	PF_A32B32G32R32F,
	PF_B8G8R8A8,
	PF_G8,
	PF_G16,
	PF_FloatRGB,
	PF_FloatRGBA,
	PF_R32_FLOAT,
	PF_G16R16,
	PF_G16R16F,
	PF_G32R32F,
	PF_A2B10G10R10,
	PF_A16B16G16R16,
	PF_R16F,
	PF_FloatR11G11B10,
	PF_A8,
	PF_R32_UINT,
	PF_R32_SINT,
	PF_R16_UINT,
	PF_R16_SINT,
	PF_R16G16B16A16_UINT,
	PF_R16G16B16A16_SINT,
	PF_R5G6B5_UNORM,
	PF_R8G8B8A8,
	PF_A8R8G8B8,
	PF_R8G8,
	PF_R32G32B32A32_UINT,
	PF_R16G16_UINT,
	PF_R8_UINT,
	PF_R8G8B8A8_UINT,
	PF_R8G8B8A8_SNORM,
	PF_R16G16B16A16_UNORM,
	PF_R16G16B16A16_SNORM,
	PF_R32G32_UINT,
	PF_R8,
};

namespace MediaCaptureDetails
{
	bool FindSceneViewportAndLevel(TSharedPtr<FSceneViewport>& OutSceneViewport);

	//Validation for the source of a capture
	bool ValidateSceneViewport(const TSharedPtr<FSceneViewport>& SceneViewport, const FMediaCaptureOptions& CaptureOption, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing);
	bool ValidateTextureRenderTarget2D(const UTextureRenderTarget2D* RenderTarget, const FMediaCaptureOptions& CaptureOption, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing);

	//Validation that there is a capture
	bool ValidateIsCapturing(const UMediaCapture& CaptureToBeValidated);

	void ShowSlateNotification();

	static const FName LevelEditorName(TEXT("LevelEditor"));
}

#if WITH_EDITOR
namespace MediaCaptureAnalytics
{
	/**
	 * @EventName MediaFramework.CaptureStarted
	 * @Trigger Triggered when a capture of the viewport or render target is started.
	 * @Type Client
	 * @Owner MediaIO Team
	 */
	void SendCaptureEvent(const FString& CaptureType)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> EventAttributes;
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CaptureType"), CaptureType));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.CaptureStarted"), EventAttributes);
		}
	}
}
#endif


/* UMediaCapture::FCaptureBaseData
*****************************************************************************/
UMediaCapture::FCaptureBaseData::FCaptureBaseData()
	: SourceFrameNumberRenderThread(0)
{

}

/* UMediaCapture::FCaptureFrame
*****************************************************************************/
UMediaCapture::FCaptureFrame::FCaptureFrame()
	: bResolvedTargetRequested(false)
{

}

/* FMediaCaptureOptions
*****************************************************************************/
FMediaCaptureOptions::FMediaCaptureOptions()
	: Crop(EMediaCaptureCroppingType::None)
	, CustomCapturePoint(FIntPoint::ZeroValue)
	, bResizeSourceBuffer(false)
	, bSkipFrameWhenRunningExpensiveTasks(true)
	, bConvertToDesiredPixelFormat(true)
	, bForceAlphaToOneOnConversion(false)
{

}


/* UMediaCapture
*****************************************************************************/

UMediaCapture::UMediaCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CurrentResolvedTargetIndex(0)
	, NumberOfCaptureFrame(2)
	, CaptureRequestCount(0)
	, MediaState(EMediaCaptureState::Stopped)
	, DesiredSize(1280, 720)
	, DesiredPixelFormat(EPixelFormat::PF_A2B10G10R10)
	, DesiredOutputSize(1280, 720)
	, DesiredOutputPixelFormat(EPixelFormat::PF_A2B10G10R10)
	, ConversionOperation(EMediaCaptureConversionOperation::NONE)
	, MediaOutputName(TEXT("[undefined]"))
	, bUseRequestedTargetSize(false)
	, bViewportHasFixedViewportSize(false)
	, bResolvedTargetInitialized(false)
	, bShouldCaptureRHITexture(false)
	, WaitingForResolveCommandExecutionCounter(0)
	, NumberOfTexturesToResolve(0)
{
}

void UMediaCapture::BeginDestroy()
{
	if (GetState() == EMediaCaptureState::Capturing || GetState() == EMediaCaptureState::Preparing)
	{
		UE_LOG(LogMediaIOCore, Warning, TEXT("%s will be destroyed and the capture was not stopped."), *GetName());
	}
	StopCapture(false);

	Super::BeginDestroy();
}

FString UMediaCapture::GetDesc()
{
	if (MediaOutput)
	{
		return FString::Printf(TEXT("%s [%s]"), *Super::GetDesc(), *MediaOutput->GetDesc());
	}
	return FString::Printf(TEXT("%s [none]"), *Super::GetDesc());
}

bool UMediaCapture::CaptureActiveSceneViewport(FMediaCaptureOptions CaptureOptions)
{
	StopCapture(false);

	check(IsInGameThread());

	TSharedPtr<FSceneViewport> FoundSceneViewport;
	if (!MediaCaptureDetails::FindSceneViewportAndLevel(FoundSceneViewport) || !FoundSceneViewport.IsValid())
	{
		UE_LOG(LogMediaIOCore, Warning, TEXT("Can not start the capture. No viewport could be found."));
		return false;
	}

	return CaptureSceneViewport(FoundSceneViewport, CaptureOptions);
}

bool UMediaCapture::CaptureSceneViewport(TSharedPtr<FSceneViewport>& InSceneViewport, FMediaCaptureOptions InCaptureOptions)
{
	StopCapture(false);

	check(IsInGameThread());

	if (!ValidateMediaOutput())
	{
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	DesiredCaptureOptions = InCaptureOptions;
	CacheMediaOutput(EMediaCaptureSourceType::SCENE_VIEWPORT);

	if (bUseRequestedTargetSize)
	{
		DesiredSize = InSceneViewport->GetSize();
	}
	else if (DesiredCaptureOptions.bResizeSourceBuffer)
	{
		SetFixedViewportSize(InSceneViewport);
	}

	CacheOutputOptions();

	const bool bCurrentlyCapturing = false;
	if (!MediaCaptureDetails::ValidateSceneViewport(InSceneViewport, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		ResetFixedViewportSize(InSceneViewport, false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	SetState(EMediaCaptureState::Preparing);
	bool bInitialized = CaptureSceneViewportImpl(InSceneViewport);

	if (bInitialized)
	{
		InitializeResolveTarget(MediaOutput->NumberOfTextureBuffers);
		bInitialized = GetState() != EMediaCaptureState::Stopped;
	}

	if (bInitialized)
	{
		//no lock required, the command on the render thread is not active
		CapturingSceneViewport = InSceneViewport;

		CurrentResolvedTargetIndex = 0;
		FCoreDelegates::OnEndFrame.AddUObject(this, &UMediaCapture::OnEndFrame_GameThread);
	}
	else
	{
		ResetFixedViewportSize(InSceneViewport, false);
		SetState(EMediaCaptureState::Stopped);
		MediaCaptureDetails::ShowSlateNotification();
	}

#if WITH_EDITOR
	MediaCaptureAnalytics::SendCaptureEvent(TEXT("SceneViewport"));
#endif
	
	return bInitialized;
}

bool UMediaCapture::CaptureTextureRenderTarget2D(UTextureRenderTarget2D* InRenderTarget2D, FMediaCaptureOptions CaptureOptions)
{
	StopCapture(false);

	check(IsInGameThread());

	if (!ValidateMediaOutput())
	{
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	DesiredCaptureOptions = CaptureOptions;
	CacheMediaOutput(EMediaCaptureSourceType::RENDER_TARGET);

	if (bUseRequestedTargetSize)
	{
		DesiredSize = FIntPoint(InRenderTarget2D->SizeX, InRenderTarget2D->SizeY);
	}
	else if (DesiredCaptureOptions.bResizeSourceBuffer)
	{
		InRenderTarget2D->ResizeTarget(DesiredSize.X, DesiredSize.Y);
	}

	CacheOutputOptions();

	const bool bCurrentlyCapturing = false;
	if (!MediaCaptureDetails::ValidateTextureRenderTarget2D(InRenderTarget2D, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	SetState(EMediaCaptureState::Preparing);
	bool bInitialized = CaptureRenderTargetImpl(InRenderTarget2D);

	if (bInitialized)
	{
		InitializeResolveTarget(MediaOutput->NumberOfTextureBuffers);
		bInitialized = GetState() != EMediaCaptureState::Stopped;
	}

	if (bInitialized)
	{
		//no lock required the command on the render thread is not active yet
		CapturingRenderTarget = InRenderTarget2D;

		CurrentResolvedTargetIndex = 0;
		FCoreDelegates::OnEndFrame.AddUObject(this, &UMediaCapture::OnEndFrame_GameThread);
	}
	else
	{
		SetState(EMediaCaptureState::Stopped);
		MediaCaptureDetails::ShowSlateNotification();
	}

#if WITH_EDITOR
	MediaCaptureAnalytics::SendCaptureEvent(TEXT("RenderTarget2D"));
#endif
	
	return bInitialized;
}

void UMediaCapture::CacheMediaOutput(EMediaCaptureSourceType InSourceType)
{
	check(MediaOutput);
	DesiredSize = MediaOutput->GetRequestedSize();
	bUseRequestedTargetSize = DesiredSize == UMediaOutput::RequestCaptureSourceSize;
	DesiredPixelFormat = MediaOutput->GetRequestedPixelFormat();
	ConversionOperation = MediaOutput->GetConversionOperation(InSourceType);
}

void UMediaCapture::CacheOutputOptions()
{
	DesiredOutputSize = GetOutputSize(DesiredSize, ConversionOperation);
	DesiredOutputPixelFormat = GetOutputPixelFormat(DesiredPixelFormat, ConversionOperation);
	MediaOutputName = *MediaOutput->GetName();
	bShouldCaptureRHITexture = ShouldCaptureRHITexture();
}

FIntPoint UMediaCapture::GetOutputSize(const FIntPoint & InSize, const EMediaCaptureConversionOperation & InConversionOperation) const
{
	switch (InConversionOperation)
	{
	case EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT:
		return FIntPoint(InSize.X / 2, InSize.Y);
	case EMediaCaptureConversionOperation::RGB10_TO_YUVv210_10BIT:
		// Padding aligned on 48 (16 and 6 at the same time)
		return FIntPoint((((InSize.X + 47) / 48) * 48) / 6, InSize.Y);
	case EMediaCaptureConversionOperation::CUSTOM:
		return GetCustomOutputSize(InSize);
	case EMediaCaptureConversionOperation::NONE:
	default:
		return InSize;
	}
}

EPixelFormat UMediaCapture::GetOutputPixelFormat(const EPixelFormat & InPixelFormat, const EMediaCaptureConversionOperation & InConversionOperation) const
{
	switch (InConversionOperation)
	{
	case EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT:
		return EPixelFormat::PF_B8G8R8A8;
	case EMediaCaptureConversionOperation::RGB10_TO_YUVv210_10BIT:
		return EPixelFormat::PF_R32G32B32A32_UINT;
	case EMediaCaptureConversionOperation::CUSTOM:
		return GetCustomOutputPixelFormat(InPixelFormat);
	case EMediaCaptureConversionOperation::NONE:
	default:
		return InPixelFormat;
	}
}

bool UMediaCapture::UpdateSceneViewport(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	if (!MediaCaptureDetails::ValidateIsCapturing(*this))
	{
		StopCapture(false);
		return false;
	}

	check(IsInGameThread());

	if (!bUseRequestedTargetSize && DesiredCaptureOptions.bResizeSourceBuffer)
	{
		SetFixedViewportSize(InSceneViewport);
	}

	const bool bCurrentlyCapturing = true;
	if (!MediaCaptureDetails::ValidateSceneViewport(InSceneViewport, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		ResetFixedViewportSize(InSceneViewport, false);
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	if (!UpdateSceneViewportImpl(InSceneViewport))
	{
		ResetFixedViewportSize(InSceneViewport, false);
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	{
		FScopeLock Lock(&AccessingCapturingSource);
		while(WaitingForResolveCommandExecutionCounter.Load() > 0)
		{
			FlushRenderingCommands();
		}
		ResetFixedViewportSize(CapturingSceneViewport.Pin(), true);
		CapturingSceneViewport = InSceneViewport;
		CapturingRenderTarget = nullptr;
	}

	return true;
}

bool UMediaCapture::UpdateTextureRenderTarget2D(UTextureRenderTarget2D * InRenderTarget2D)
{
	if (!MediaCaptureDetails::ValidateIsCapturing(*this))
	{
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	check(IsInGameThread());

	if (!bUseRequestedTargetSize && DesiredCaptureOptions.bResizeSourceBuffer)
	{
		InRenderTarget2D->ResizeTarget(DesiredSize.X, DesiredSize.Y);
	}

	const bool bCurrentlyCapturing = true;
	if (!MediaCaptureDetails::ValidateTextureRenderTarget2D(InRenderTarget2D, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	if (!UpdateRenderTargetImpl(InRenderTarget2D))
	{
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	{
		FScopeLock Lock(&AccessingCapturingSource);
		while (WaitingForResolveCommandExecutionCounter.Load() > 0)
		{
			FlushRenderingCommands();
		}
		ResetFixedViewportSize(CapturingSceneViewport.Pin(), true);
		CapturingRenderTarget = InRenderTarget2D;
		CapturingSceneViewport.Reset();
	}

	return true;
}

void UMediaCapture::StopCapture(bool bAllowPendingFrameToBeProcess)
{
	check(IsInGameThread());

	if (GetState() != EMediaCaptureState::StopRequested && GetState() != EMediaCaptureState::Capturing)
	{
		bAllowPendingFrameToBeProcess = false;
	}

	if (bAllowPendingFrameToBeProcess)
	{
		if (GetState() != EMediaCaptureState::Stopped && GetState() != EMediaCaptureState::StopRequested)
		{
			SetState(EMediaCaptureState::StopRequested);

			while (WaitingForResolveCommandExecutionCounter.Load() > 0)
			{
				FlushRenderingCommands();
			}
		}
	}
	else
	{
		if (GetState() != EMediaCaptureState::Stopped)
		{
			SetState(EMediaCaptureState::Stopped);

			FCoreDelegates::OnEndFrame.RemoveAll(this);

			while (WaitingForResolveCommandExecutionCounter.Load() > 0 || !bResolvedTargetInitialized)
			{
				FlushRenderingCommands();
			}
			StopCaptureImpl(bAllowPendingFrameToBeProcess);
			ResetFixedViewportSize(CapturingSceneViewport.Pin(), false);

			CapturingRenderTarget = nullptr;
			CapturingSceneViewport.Reset();
			DesiredSize = FIntPoint(1280, 720);
			DesiredPixelFormat = EPixelFormat::PF_A2B10G10R10;
			DesiredOutputSize = FIntPoint(1280, 720);
			DesiredOutputPixelFormat = EPixelFormat::PF_A2B10G10R10;
			DesiredCaptureOptions = FMediaCaptureOptions();
			ConversionOperation = EMediaCaptureConversionOperation::NONE;
			MediaOutputName.Reset();

			// CaptureFrames contains FTexture2DRHIRef, therefore should be released on Render Tread thread.
			// Keep references frames to be released in a temporary array and clear CaptureFrames on Game Thread.
			TSharedPtr<TArray<FCaptureFrame>> TempArrayToBeReleasedOnRenderThread = MakeShared<TArray<FCaptureFrame>>();
			*TempArrayToBeReleasedOnRenderThread = MoveTemp(CaptureFrames);
			ENQUEUE_RENDER_COMMAND(MediaOutputReleaseCaptureFrames)(
				[TempArrayToBeReleasedOnRenderThread](FRHICommandListImmediate& RHICmdList)
				{
					TempArrayToBeReleasedOnRenderThread->Reset();
				});
		}
	}
}

void UMediaCapture::SetMediaOutput(UMediaOutput* InMediaOutput)
{
	if (GetState() == EMediaCaptureState::Stopped)
	{
		MediaOutput = InMediaOutput;
	}
}

void UMediaCapture::SetState(EMediaCaptureState InNewState)
{
	if (MediaState != InNewState)
	{
		MediaState = InNewState;
		if (IsInGameThread())
		{
			BroadcastStateChanged();
		}
		else
		{
			TWeakObjectPtr<UMediaCapture> Self = this;
			AsyncTask(ENamedThreads::GameThread, [Self]
			{
				UMediaCapture* MediaCapture = Self.Get();
				if (UObjectInitialized() && MediaCapture)
				{
					MediaCapture->BroadcastStateChanged();
				}
			});
		}
	}
}

void UMediaCapture::BroadcastStateChanged()
{
	OnStateChanged.Broadcast();
	OnStateChangedNative.Broadcast();
}

void UMediaCapture::SetFixedViewportSize(TSharedPtr<FSceneViewport> InSceneViewport)
{
	InSceneViewport->SetFixedViewportSize(DesiredSize.X, DesiredSize.Y);
	bViewportHasFixedViewportSize = true;
}

void UMediaCapture::ResetFixedViewportSize(TSharedPtr<FSceneViewport> InViewport, bool bInFlushRenderingCommands)
{
	if (bViewportHasFixedViewportSize && InViewport.IsValid())
	{
		if (bInFlushRenderingCommands && WaitingForResolveCommandExecutionCounter.Load() > 0)
		{
			FlushRenderingCommands();
		}
		InViewport->SetFixedViewportSize(0, 0);
		bViewportHasFixedViewportSize = false;
	}
}

bool UMediaCapture::HasFinishedProcessing() const
{
	return WaitingForResolveCommandExecutionCounter.Load() == 0
		|| GetState() == EMediaCaptureState::Error
		|| GetState() == EMediaCaptureState::Stopped;
}

void UMediaCapture::InitializeResolveTarget(int32 InNumberOfBuffers)
{
	if (DesiredOutputSize.X <= 0 || DesiredOutputSize.Y <= 0)
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can't start the capture. The size requested is negative or zero."));
		SetState(EMediaCaptureState::Stopped);
		return;
	}

	if (bShouldCaptureRHITexture)
	{
		// No buffer is needed if the callback is with the RHI Texture
		InNumberOfBuffers = 1;
	}

	NumberOfCaptureFrame = InNumberOfBuffers;
	check(CaptureFrames.Num() == 0);
	CaptureFrames.AddDefaulted(InNumberOfBuffers);

	// Only create CPU readback texture when we are using the CPU callback
	if (!bShouldCaptureRHITexture)
	{
		UMediaCapture* This = this;
		ENQUEUE_RENDER_COMMAND(MediaOutputCaptureFrameCreateTexture)(
			[This](FRHICommandListImmediate& RHICmdList)
			{
				FRHIResourceCreateInfo CreateInfo;
				for (int32 Index = 0; Index < This->NumberOfCaptureFrame; ++Index)
				{
					This->CaptureFrames[Index].ReadbackTexture = RHICreateTexture2D(
						This->DesiredOutputSize.X,
						This->DesiredOutputSize.Y,
						This->DesiredOutputPixelFormat,
						1,
						1,
						TexCreate_CPUReadback,
						CreateInfo
					);
				}
				This->bResolvedTargetInitialized = true;
			});
	}
	else
	{
		bResolvedTargetInitialized = true;
	}
}

bool UMediaCapture::ValidateMediaOutput() const
{
	if (MediaOutput == nullptr)
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can not start the capture. The Media Output is invalid."));
		return false;
	}

	FString FailureReason;
	if (!MediaOutput->Validate(FailureReason))
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can not start the capture. %s."), *FailureReason);
		return false;
	}

	return true;
}

void UMediaCapture::OnEndFrame_GameThread()
{
	if (!bResolvedTargetInitialized)
	{
		FlushRenderingCommands();
	}

	if (!MediaOutput)
	{
		return;
	}

	if (GetState() == EMediaCaptureState::Error)
	{
		StopCapture(false);
	}

	if (GetState() != EMediaCaptureState::Capturing && GetState() != EMediaCaptureState::StopRequested)
	{
		return;
	}

	if (DesiredCaptureOptions.bSkipFrameWhenRunningExpensiveTasks && !FSlateThrottleManager::Get().IsAllowingExpensiveTasks())
	{
		return;
	}

	CurrentResolvedTargetIndex = (CurrentResolvedTargetIndex + 1) % NumberOfCaptureFrame;
	int32 ReadyFrameIndex = (CurrentResolvedTargetIndex + 1) % NumberOfCaptureFrame; // Next one in the buffer queue

	// Frame that should be on the system ram and we want to send to the user
	FCaptureFrame* ReadyFrame = &CaptureFrames[ReadyFrameIndex];
	// Frame that we want to transfer to system ram
	FCaptureFrame* CapturingFrame = (GetState() != EMediaCaptureState::StopRequested) ? &CaptureFrames[CurrentResolvedTargetIndex] : nullptr;

	UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("MediaOutput: '%s'. ReadyFrameIndex: '%d' '%s'. CurrentResolvedTargetIndex: '%d'.")
		, *MediaOutputName, ReadyFrameIndex, (CaptureFrames[ReadyFrameIndex].bResolvedTargetRequested) ? TEXT("Y"): TEXT("N"), CurrentResolvedTargetIndex);
	if (GetState() == EMediaCaptureState::StopRequested && NumberOfTexturesToResolve.Load() <= 0)
	{
		// All the requested frames have been captured.
		StopCapture(false);
		return;
	}

	if (CapturingFrame)
	{
		//Verify if game thread is overrunning the render thread.
		if (CapturingFrame->bResolvedTargetRequested)
		{
			FlushRenderingCommands();
		}

		CapturingFrame->CaptureBaseData.SourceFrameTimecode = FApp::GetTimecode();
		CapturingFrame->CaptureBaseData.SourceFrameTimecodeFramerate = FApp::GetTimecodeFrameRate();
		CapturingFrame->CaptureBaseData.SourceFrameNumberRenderThread = GFrameNumber;
		CapturingFrame->CaptureBaseData.SourceFrameNumber = ++CaptureRequestCount;
		CapturingFrame->UserData = GetCaptureFrameUserData_GameThread();
	}

	// Init variables for ENQUEUE_RENDER_COMMAND.
	//The Lock only synchronize while we are copying the value to the enqueue. The viewport and the rendertarget may change while we are in the enqueue command.
	{
		FScopeLock Lock(&AccessingCapturingSource);

		TSharedPtr<FSceneViewport> CapturingSceneViewportPin = CapturingSceneViewport.Pin();
		FSceneViewport* InCapturingSceneViewport = CapturingSceneViewportPin.Get();
		FTextureRenderTargetResource* InTextureRenderTargetResource = CapturingRenderTarget ? CapturingRenderTarget->GameThread_GetRenderTargetResource() : nullptr;
		FIntPoint InDesiredSize = DesiredSize;
		FMediaCaptureStateChangedSignature InOnStateChanged = OnStateChanged;
		UMediaCapture* InMediaCapture = this;

		if (InCapturingSceneViewport != nullptr || InTextureRenderTargetResource != nullptr)
		{
			++WaitingForResolveCommandExecutionCounter;

			// RenderCommand to be executed on the RenderThread
			ENQUEUE_RENDER_COMMAND(FMediaOutputCaptureFrameCreateTexture)(
				[InMediaCapture, CapturingFrame, ReadyFrame, InCapturingSceneViewport, InTextureRenderTargetResource, InDesiredSize, InOnStateChanged](FRHICommandListImmediate& RHICmdList)
			{
				InMediaCapture->Capture_RenderThread(RHICmdList, InMediaCapture, CapturingFrame, ReadyFrame, InCapturingSceneViewport, InTextureRenderTargetResource, InDesiredSize, InOnStateChanged);
			});
		}
	}
}

void UMediaCapture::Capture_RenderThread(FRHICommandListImmediate& RHICmdList,
	UMediaCapture* InMediaCapture,
	FCaptureFrame* CapturingFrame,
	FCaptureFrame* ReadyFrame,
	FSceneViewport* InCapturingSceneViewport,
	FTextureRenderTargetResource* InTextureRenderTargetResource,
	FIntPoint InDesiredSize,
	FMediaCaptureStateChangedSignature InOnStateChanged)
{
	FTexture2DRHIRef SourceTexture;

	{
		if (InCapturingSceneViewport)
		{
#if WITH_EDITOR
			if (!IsRunningGame())
			{
				// PIE, PIE in windows, editor viewport
				SourceTexture = InCapturingSceneViewport->GetRenderTargetTexture();
				if (!SourceTexture.IsValid() && InCapturingSceneViewport->GetViewportRHI())
				{
					SourceTexture = RHICmdList.GetViewportBackBuffer(InCapturingSceneViewport->GetViewportRHI());
				}
			}
			else
#endif
				if (InCapturingSceneViewport->GetViewportRHI())
				{
					// Standalone and packaged
					SourceTexture = RHICmdList.GetViewportBackBuffer(InCapturingSceneViewport->GetViewportRHI());
				}
		}
		else if (InTextureRenderTargetResource && InTextureRenderTargetResource->GetTextureRenderTarget2DResource())
		{
			SourceTexture = InTextureRenderTargetResource->GetTextureRenderTarget2DResource()->GetTextureRHI();
		}
	}

	if (!SourceTexture.IsValid())
	{
		InMediaCapture->SetState(EMediaCaptureState::Error);
		UE_LOG(LogMediaIOCore, Error, TEXT("Can't grab the Texture to capture for '%s'."), *InMediaCapture->MediaOutputName);
	}
	else if (CapturingFrame)
	{
		// If it is a simple rgba swizzle we can handle the conversion. Supported formats
		// contained in SupportedRgbaSwizzleFormats. Warning would've been displayed on start of capture.
		if (InMediaCapture->DesiredPixelFormat != SourceTexture->GetFormat() && 
			(!SupportedRgbaSwizzleFormats.Contains(SourceTexture->GetFormat()) || !InMediaCapture->DesiredCaptureOptions.bConvertToDesiredPixelFormat))
		{
			InMediaCapture->SetState(EMediaCaptureState::Error);
			UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source pixel format doesn't match with the user requested pixel format. %sRequested: %s Source: %s")
				, *InMediaCapture->MediaOutputName
				, (SupportedRgbaSwizzleFormats.Contains(SourceTexture->GetFormat()) && !InMediaCapture->DesiredCaptureOptions.bConvertToDesiredPixelFormat) ? TEXT("Please enable \"Convert To Desired Pixel Format\" option in Media Capture settings. ") : TEXT("")
				, GetPixelFormatString(InMediaCapture->DesiredPixelFormat)
				, GetPixelFormatString(SourceTexture->GetFormat()));
		}

		if (InMediaCapture->DesiredCaptureOptions.Crop == EMediaCaptureCroppingType::None)
		{
			if (InDesiredSize.X != SourceTexture->GetSizeX() || InDesiredSize.Y != SourceTexture->GetSizeY())
			{
				InMediaCapture->SetState(EMediaCaptureState::Error);
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source size doesn't match with the user requested size. Requested: %d,%d  Source: %d,%d")
					, *InMediaCapture->MediaOutputName
					, InDesiredSize.X, InDesiredSize.Y
					, SourceTexture->GetSizeX(), SourceTexture->GetSizeY());
			}
		}
		else
		{
			FIntPoint StartCapturePoint = FIntPoint::ZeroValue;
			if (InMediaCapture->DesiredCaptureOptions.Crop == EMediaCaptureCroppingType::Custom)
			{
				StartCapturePoint = InMediaCapture->DesiredCaptureOptions.CustomCapturePoint;
			}

			if ((uint32)(InDesiredSize.X + StartCapturePoint.X) > SourceTexture->GetSizeX() || (uint32)(InDesiredSize.Y + StartCapturePoint.Y) > SourceTexture->GetSizeY())
			{
				InMediaCapture->SetState(EMediaCaptureState::Error);
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source size doesn't match with the user requested size. Requested: %d,%d  Source: %d,%d")
					, *InMediaCapture->MediaOutputName
					, InDesiredSize.X, InDesiredSize.Y
					, SourceTexture->GetSizeX(), SourceTexture->GetSizeY());
			}
		}
	}

	if (CapturingFrame && InMediaCapture->GetState() != EMediaCaptureState::Error)
	{
		SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_CopyToResolve);

		FPooledRenderTargetDesc OutputDesc = FPooledRenderTargetDesc::Create2DDesc(
			InMediaCapture->DesiredOutputSize,
			InMediaCapture->DesiredOutputPixelFormat,
			FClearValueBinding::None,
			TexCreate_None,
			TexCreate_RenderTargetable,
			false);
		TRefCountPtr<IPooledRenderTarget> ResampleTexturePooledRenderTarget;
		GRenderTargetPool.FindFreeElement(RHICmdList, OutputDesc, ResampleTexturePooledRenderTarget, TEXT("MediaCapture"));
		const FSceneRenderTargetItem& DestRenderTarget = ResampleTexturePooledRenderTarget->GetRenderTargetItem();

		// Do we need to crop
		float ULeft = 0.0f;
		float URight = 1.0f;
		float VTop = 0.0f;
		float VBottom = 1.0f;
		FResolveParams ResolveParams;
		if (InMediaCapture->DesiredCaptureOptions.Crop != EMediaCaptureCroppingType::None)
		{
			switch (InMediaCapture->DesiredCaptureOptions.Crop)
			{
			case EMediaCaptureCroppingType::Center:
				ResolveParams.Rect = FResolveRect((SourceTexture->GetSizeX() - InDesiredSize.X) / 2, (SourceTexture->GetSizeY() - InDesiredSize.Y) / 2, 0, 0);
				ResolveParams.Rect.X2 = ResolveParams.Rect.X1 + InDesiredSize.X;
				ResolveParams.Rect.Y2 = ResolveParams.Rect.Y1 + InDesiredSize.Y;
				break;
			case EMediaCaptureCroppingType::TopLeft:
				ResolveParams.Rect = FResolveRect(0, 0, InDesiredSize.X, InDesiredSize.Y);
				break;
			case EMediaCaptureCroppingType::Custom:
				ResolveParams.Rect = FResolveRect(InMediaCapture->DesiredCaptureOptions.CustomCapturePoint.X, InMediaCapture->DesiredCaptureOptions.CustomCapturePoint.Y, 0, 0);
				ResolveParams.Rect.X2 = ResolveParams.Rect.X1 + InDesiredSize.X;
				ResolveParams.Rect.Y2 = ResolveParams.Rect.Y1 + InDesiredSize.Y;
				break;
			}

			ResolveParams.DestRect.X1 = 0;
			ResolveParams.DestRect.X2 = InDesiredSize.X;
			ResolveParams.DestRect.Y1 = 0;
			ResolveParams.DestRect.Y2 = InDesiredSize.Y;

			ULeft = (float)ResolveParams.Rect.X1 / (float)SourceTexture->GetSizeX();
			URight = (float)ResolveParams.Rect.X2 / (float)SourceTexture->GetSizeX();
			VTop = (float)ResolveParams.Rect.Y1 / (float)SourceTexture->GetSizeY();
			VBottom = (float)ResolveParams.Rect.Y2 / (float)SourceTexture->GetSizeY();
		}

		{
			SCOPED_DRAW_EVENTF(RHICmdList, MediaCapture, TEXT("MediaCapture"));

			bool bRequiresFormatConversion = InMediaCapture->DesiredPixelFormat != SourceTexture->GetFormat();

			if (InMediaCapture->ConversionOperation == EMediaCaptureConversionOperation::NONE && !bRequiresFormatConversion)
			{
				// Asynchronously copy target from GPU to GPU
				RHICmdList.CopyToResolveTarget(SourceTexture, DestRenderTarget.TargetableTexture, ResolveParams);
			}
			else if (InMediaCapture->ConversionOperation == EMediaCaptureConversionOperation::CUSTOM)
			{
				InMediaCapture->OnCustomCapture_RenderingThread(RHICmdList, CapturingFrame->CaptureBaseData, CapturingFrame->UserData
					, SourceTexture, DestRenderTarget.TargetableTexture, ResolveParams, {ULeft, URight}, {VTop, VBottom});
			}
			else
			{
				// convert the source with a draw call
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FRHITexture* RenderTarget = DestRenderTarget.TargetableTexture.GetReference();
				RHICmdList.Transition(FRHITransitionInfo(RenderTarget, ERHIAccess::Unknown, ERHIAccess::RTV));
				FRHIRenderPassInfo RPInfo(RenderTarget,  ERenderTargetActions::DontLoad_Store);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("MediaCapture"));

				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

				// configure media shaders
				auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
				TShaderMapRef<FMediaShadersVS> VertexShader(ShaderMap);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();

				const bool bDoLinearToSRGB = false;

				switch (InMediaCapture->ConversionOperation)
				{
				case EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT:
				{
					TShaderMapRef<FRGB8toUYVY8ConvertPS> ConvertShader(ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					ConvertShader->SetParameters(RHICmdList, SourceTexture, MediaShaders::RgbToYuvRec709Full, MediaShaders::YUVOffset8bits, bDoLinearToSRGB);
				}
				break;
				case EMediaCaptureConversionOperation::RGB10_TO_YUVv210_10BIT:
				{
					TShaderMapRef<FRGB10toYUVv210ConvertPS> ConvertShader(ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					ConvertShader->SetParameters(RHICmdList, SourceTexture, MediaShaders::RgbToYuvRec709Full, MediaShaders::YUVOffset10bits, bDoLinearToSRGB);
				}
				break;
				case EMediaCaptureConversionOperation::INVERT_ALPHA:
					// fall through
				case EMediaCaptureConversionOperation::SET_ALPHA_ONE:
					// fall through
				case EMediaCaptureConversionOperation::NONE:
					bRequiresFormatConversion = true;
				default:
					if (bRequiresFormatConversion)
					{
						FModifyAlphaSwizzleRgbaPS::FPermutationDomain PermutationVector;
						// In cases where texture is converted from a format that doesn't have A channel, we want to force set it to 1.
						EMediaCaptureConversionOperation MediaConversionOperation = InMediaCapture->DesiredCaptureOptions.bForceAlphaToOneOnConversion ? EMediaCaptureConversionOperation::SET_ALPHA_ONE : InMediaCapture->ConversionOperation;
						PermutationVector.Set<FModifyAlphaSwizzleRgbaPS::FConversionOp>(static_cast<int32>(MediaConversionOperation));
						TShaderMapRef<FModifyAlphaSwizzleRgbaPS> ConvertShader(ShaderMap, PermutationVector);
						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = ConvertShader.GetPixelShader();
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
						ConvertShader->SetParameters(RHICmdList, SourceTexture);
					}
				break;
				}

				// draw full size quad into render target
				FVertexBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer(ULeft, URight, VTop, VBottom);
				RHICmdList.SetStreamSource(0, VertexBuffer, 0);

				// set viewport to RT size
				RHICmdList.SetViewport(0, 0, 0.0f, InMediaCapture->DesiredOutputSize.X, InMediaCapture->DesiredOutputSize.Y, 1.0f);
				RHICmdList.DrawPrimitive(0, 2, 1);
				RHICmdList.EndRenderPass();
				RHICmdList.Transition(FRHITransitionInfo(DestRenderTarget.TargetableTexture, ERHIAccess::RTV, ERHIAccess::SRVGraphics));

			}

			if (InMediaCapture->bShouldCaptureRHITexture)
			{
				SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_Callback);
				InMediaCapture->OnRHITextureCaptured_RenderingThread(CapturingFrame->CaptureBaseData, CapturingFrame->UserData, DestRenderTarget.TargetableTexture);
				CapturingFrame->bResolvedTargetRequested = false;
			}
			else
			{
				// Asynchronously copy duplicate target from GPU to System Memory
				RHICmdList.CopyToResolveTarget(DestRenderTarget.TargetableTexture, CapturingFrame->ReadbackTexture, FResolveParams());
				CapturingFrame->bResolvedTargetRequested = true;
				++NumberOfTexturesToResolve;
			}
		}
	}

	if (!InMediaCapture->bShouldCaptureRHITexture && InMediaCapture->GetState() != EMediaCaptureState::Error)
	{
		if (ReadyFrame->bResolvedTargetRequested)
		{
#if WITH_MGPU
			FRHIGPUMask GPUMask = RHICmdList.GetGPUMask();

			// If GPUMask is not set to a specific GPU we and since we are reading back the texture, it shouldn't matter which GPU we do this on.
			if (!GPUMask.HasSingleIndex())
			{
				GPUMask = FRHIGPUMask::FromIndex(GPUMask.GetFirstIndex());
			}

			SCOPED_GPU_MASK(RHICmdList, GPUMask);
#endif
			check(ReadyFrame->ReadbackTexture.IsValid());

			// Lock & read
			void* ColorDataBuffer = nullptr;
			int32 Width = 0, Height = 0;
			{
				SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_MapStaging);
				RHICmdList.MapStagingSurface(ReadyFrame->ReadbackTexture, ColorDataBuffer, Width, Height);
			}
			{
				SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_Callback);
				InMediaCapture->OnFrameCaptured_RenderingThread(ReadyFrame->CaptureBaseData, ReadyFrame->UserData, ColorDataBuffer, Width, Height);
			}
			ReadyFrame->bResolvedTargetRequested = false;
			--NumberOfTexturesToResolve;

			RHICmdList.UnmapStagingSurface(ReadyFrame->ReadbackTexture);
		}
	}

	--InMediaCapture->WaitingForResolveCommandExecutionCounter;
}

/* namespace MediaCaptureDetails implementation
*****************************************************************************/
namespace MediaCaptureDetails
{
	bool FindSceneViewportAndLevel(TSharedPtr<FSceneViewport>& OutSceneViewport)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
					FSlatePlayInEditorInfo& Info = EditorEngine->SlatePlayInEditorMap.FindChecked(Context.ContextHandle);

					// The PIE window has priority over the regular editor window, so we need to break out of the loop if either of these are found
					if (TSharedPtr<IAssetViewport> DestinationLevelViewport = Info.DestinationSlateViewport.Pin())
					{
						OutSceneViewport = DestinationLevelViewport->GetSharedActiveViewport();
						break;
					}
					else if (Info.SlatePlayInEditorWindowViewport.IsValid())
					{
						OutSceneViewport = Info.SlatePlayInEditorWindowViewport;
						break;
					}
				}
				else if (Context.WorldType == EWorldType::Editor)
				{
					if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(LevelEditorName))
					{
						TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveViewport();
						if (ActiveLevelViewport.IsValid())
						{
							OutSceneViewport = ActiveLevelViewport->GetSharedActiveViewport();
						}
					}
				}
			}
		}
		else
#endif
		{
			UGameEngine* GameEngine = CastChecked<UGameEngine>(GEngine);
			OutSceneViewport = GameEngine->SceneViewport;
		}

		return (OutSceneViewport.IsValid());
	}

	bool ValidateSize(const FIntPoint TargetSize, const FIntPoint& DesiredSize, const FMediaCaptureOptions& CaptureOptions, const bool bCurrentlyCapturing)
	{
		if (CaptureOptions.Crop == EMediaCaptureCroppingType::None)
		{
			if (DesiredSize.X != TargetSize.X || DesiredSize.Y != TargetSize.Y)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target size doesn't match with the requested size. SceneViewport: %d,%d  MediaOutput: %d,%d")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, TargetSize.X, TargetSize.Y
					, DesiredSize.X, DesiredSize.Y);
				return false;
			}
		}
		else
		{
			FIntPoint StartCapturePoint = FIntPoint::ZeroValue;
			if (CaptureOptions.Crop == EMediaCaptureCroppingType::Custom)
			{
				if (CaptureOptions.CustomCapturePoint.X < 0 || CaptureOptions.CustomCapturePoint.Y < 0)
				{
					UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The start capture point is negatif. Start Point: %d,%d")
						, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
						, StartCapturePoint.X, StartCapturePoint.Y);
					return false;
				}
				StartCapturePoint = CaptureOptions.CustomCapturePoint;
			}

			if (DesiredSize.X + StartCapturePoint.X > TargetSize.X || DesiredSize.Y + StartCapturePoint.Y > TargetSize.Y)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target size is too small for the requested cropping options. SceneViewport: %d,%d  MediaOutput: %d,%d Start Point: %d,%d")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, TargetSize.X, TargetSize.Y
					, DesiredSize.X, DesiredSize.Y
					, StartCapturePoint.X, StartCapturePoint.Y);
				return false;
			}
		}

		return true;
	}

	bool ValidateSceneViewport(const TSharedPtr<FSceneViewport>& SceneViewport, const FMediaCaptureOptions& CaptureOptions, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing)
	{
		if (!SceneViewport.IsValid())
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Scene Viewport is invalid.")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start"));
			return false;
		}

		const FIntPoint SceneViewportSize = SceneViewport->GetRenderTargetTextureSizeXY();
		if (!ValidateSize(SceneViewportSize, DesiredSize, CaptureOptions, bCurrentlyCapturing))
		{
			return false;
		}

		static const auto CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));
		if (DesiredPixelFormat != SceneTargetFormat)
		{
			if (!SupportedRgbaSwizzleFormats.Contains(SceneTargetFormat) || !CaptureOptions.bConvertToDesiredPixelFormat)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target pixel format doesn't match with the requested pixel format. %sRenderTarget: %s MediaOutput: %s")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, (SupportedRgbaSwizzleFormats.Contains(SceneTargetFormat) && !CaptureOptions.bConvertToDesiredPixelFormat) ? TEXT("Please enable \"Convert To Desired Pixel Format\" option in Media Capture settings") : TEXT("")
					, GetPixelFormatString(SceneTargetFormat)
					, GetPixelFormatString(DesiredPixelFormat));
				return false;
			}
			else
			{
				UE_LOG(LogMediaIOCore, Warning, TEXT("The Render Target pixel format doesn't match with the requested pixel format. Render target will be automatically converted. This could have a slight performance impact. RenderTarget: %s MediaOutput: %s")
					, GetPixelFormatString(SceneTargetFormat)
					, GetPixelFormatString(DesiredPixelFormat));
			}
		}

		return true;
	}

	bool ValidateTextureRenderTarget2D(const UTextureRenderTarget2D* InRenderTarget2D, const FMediaCaptureOptions& CaptureOptions, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing)
	{
		if (InRenderTarget2D == nullptr)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Couldn't %s the capture. The Render Target is invalid.")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start"));
			return false;
		}

		if (!ValidateSize(FIntPoint(InRenderTarget2D->SizeX, InRenderTarget2D->SizeY), DesiredSize, CaptureOptions, bCurrentlyCapturing))
		{
			return false;
		}

		if (DesiredPixelFormat != InRenderTarget2D->GetFormat())
		{
			if (!SupportedRgbaSwizzleFormats.Contains(InRenderTarget2D->GetFormat()) || !CaptureOptions.bConvertToDesiredPixelFormat)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target pixel format doesn't match with the requested pixel format. %sRenderTarget: %s MediaOutput: %s")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, (SupportedRgbaSwizzleFormats.Contains(InRenderTarget2D->GetFormat()) && !CaptureOptions.bConvertToDesiredPixelFormat) ? TEXT("Please enable \"Convert To Desired Pixel Format\" option in Media Capture settings. ") : TEXT("")
					, GetPixelFormatString(InRenderTarget2D->GetFormat())
					, GetPixelFormatString(DesiredPixelFormat));
				return false;
			}
			else
			{
				UE_LOG(LogMediaIOCore, Warning, TEXT("The Render Target pixel format doesn't match with the requested pixel format. Render target will be automatically converted. This could have a slight performance impact. RenderTarget: %s MediaOutput: %s")
					, GetPixelFormatString(InRenderTarget2D->GetFormat())
					, GetPixelFormatString(DesiredPixelFormat));
			}
		}

		return true;
	}

	bool ValidateIsCapturing(const UMediaCapture& CaptureToBeValidated)
	{
		if (CaptureToBeValidated.GetState() != EMediaCaptureState::Capturing && CaptureToBeValidated.GetState() != EMediaCaptureState::Preparing)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not update the capture. There is no capture currently.\
			Only use UpdateSceneViewport or UpdateTextureRenderTarget2D when the state is Capturing or Preparing"));
			return false;
		}

		return true;
	}

	void ShowSlateNotification()
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			static double PreviousWarningTime = 0.0;
			const double TimeNow = FPlatformTime::Seconds();
			const double TimeBetweenWarningsInSeconds = 3.0f;

			if (TimeNow - PreviousWarningTime > TimeBetweenWarningsInSeconds)
			{
				FNotificationInfo NotificationInfo(LOCTEXT("MediaCaptureFailedError", "The media failed to capture. Check Output Log for details!"));
				NotificationInfo.ExpireDuration = 2.0f;
				FSlateNotificationManager::Get().AddNotification(NotificationInfo);

				PreviousWarningTime = TimeNow;
			}
		}
#endif // WITH_EDITOR
	}
}

#undef LOCTEXT_NAMESPACE
