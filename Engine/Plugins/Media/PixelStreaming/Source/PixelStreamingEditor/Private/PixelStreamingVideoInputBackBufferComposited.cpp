// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVideoInputBackBufferComposited.h"
#include "Settings.h"
#include "PixelStreamingEditorUtils.h"
#include "Utils.h"
#include "UtilsRender.h"
#include "ToStringExtensions.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingPrivate.h"
#include "PixelCaptureInputFrameRHI.h"
#include "PixelCaptureCapturerRHI.h"
#include "PixelCaptureCapturerRHIToI420CPU.h"
#include "PixelCaptureCapturerRHIToI420Compute.h"
#include "PixelCaptureBufferFormat.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindowDefinition.h"
#include "Application/SlateApplicationBase.h"
#include "Widgets/SWindow.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreamingBackBufferComposited, Log, VeryVerbose);
DEFINE_LOG_CATEGORY(LogPixelStreamingBackBufferComposited);

TSharedPtr<FPixelStreamingVideoInputBackBufferComposited> FPixelStreamingVideoInputBackBufferComposited::Create()
{
	TSharedPtr<FPixelStreamingVideoInputBackBufferComposited> NewInput = TSharedPtr<FPixelStreamingVideoInputBackBufferComposited>(new FPixelStreamingVideoInputBackBufferComposited());
	TWeakPtr<FPixelStreamingVideoInputBackBufferComposited> WeakInput = NewInput;
	// Set up the callback on the game thread since FSlateApplication::Get() can only be used there
	UE::PixelStreaming::DoOnGameThread([WeakInput]() {
		if (TSharedPtr<FPixelStreamingVideoInputBackBufferComposited> Input = WeakInput.Pin())
		{
			FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
			Input->DelegateHandle = Renderer->OnBackBufferReadyToPresent().AddSP(Input.ToSharedRef(), &FPixelStreamingVideoInputBackBufferComposited::OnBackBufferReady);
		}
	});

	return NewInput;
}

FPixelStreamingVideoInputBackBufferComposited::FPixelStreamingVideoInputBackBufferComposited()
{
	CompositedFrameSize = MakeShared<FIntPoint>();
	UE::PixelStreaming::DoOnGameThread([this]() {
		FSlateApplication::Get().OnPreTick().AddLambda([this](float DeltaTime) {
			TopLevelWindowsCriticalSection.Lock();
			TopLevelWindows.Empty();
			FSlateApplication::Get().GetAllVisibleWindowsOrdered(TopLevelWindows);
			TopLevelWindowsCriticalSection.Unlock();
		});
	});
}

FPixelStreamingVideoInputBackBufferComposited::~FPixelStreamingVideoInputBackBufferComposited()
{
	if (!IsEngineExitRequested())
	{
		UE::PixelStreaming::DoOnGameThread([HandleCopy = DelegateHandle]() {
			FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(HandleCopy);
		});
	}
}

void FPixelStreamingVideoInputBackBufferComposited::OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
{
	{
		FScopeLock Lock(&TopLevelWindowsCriticalSection);
		if (TopLevelWindows.IsEmpty())
		{
			return;
		}
	}

	UE_LOG(LogPixelStreamingBackBufferComposited, Verbose, TEXT("=== Window Rendered ==="));
	UE_LOG(LogPixelStreamingBackBufferComposited, Verbose, TEXT("Type: %s"), UE::EditorPixelStreaming::ToString(SlateWindow.GetType()));

	uint32 FrameX = FrameBuffer->GetSizeXY().X;
	uint32 FrameY = FrameBuffer->GetSizeXY().Y;
	FString Hash = UE::EditorPixelStreaming::HashWindow(SlateWindow, FrameBuffer);
	FTextureRHIRef StagingTexture = StagingTextures.FindRef(Hash);
	if(!StagingTexture.IsValid()) 
	{
		UE_LOG(LogPixelStreamingBackBufferComposited, Verbose, TEXT("Creating new staging texture: %dx%d"), FrameX, FrameY);
		StagingTexture = UE::PixelStreaming::CreateRHITexture(FrameX, FrameY);
		StagingTextures.Add(Hash, StagingTexture);
	}


	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	// Re-render FrameBuffer to StagingTexture (ensure format match)
	UE::PixelStreaming::CopyTextureRDG(RHICmdList, FrameBuffer, StagingTexture);
	
	TopLevelWindowTextures.Add(&SlateWindow, StagingTexture);
	TopLevelWindowsCriticalSection.Lock();
	// Check that we have received a texture from every window in the TopLevelWindows array
	uint8 WindowsRendered = 0;
	for (TSharedRef<SWindow> Window : TopLevelWindows)
	{
		if (TopLevelWindowTextures.FindRef(&Window.Get()))
		{
			++WindowsRendered;
		}
	}

	
	if (WindowsRendered == TopLevelWindows.Num())
	{
		// We have all the textures needed, let's composite them
		// Need to iterate over the TopLevelWindows array as this array contains the ordered windows
		FTextureRHIRef CompositedFrame;
		for (TSharedRef<SWindow> Window : TopLevelWindows)
		{
			FTextureRHIRef Texture = TopLevelWindowTextures.FindRef(&Window.Get());
			if (Window->GetOpacity() == 0.0f || 
				Window->GetType() == EWindowType::CursorDecorator ||
				Window->GetSizeInScreen() == FVector2D(0, 0))
			{
				continue;
			}	

			if(!CompositedFrame.IsValid()) 
			{
				// This is the first texture being processed, make it our base for the composited frame
				CompositedFrame = Texture;
				continue;
			}

			uint32 CompositedX = CompositedFrame->GetSizeXY().X;
			uint32 CompositedY = CompositedFrame->GetSizeXY().Y;
			// Use the absolute value for window positions. This is due to main windows having a position of (-8, -8) which can lead to crashes when copying textures
			uint32 TotalX = FMath::Min(Texture->GetSizeXY().X, Window->GetSizeInScreen().IntPoint().X) + (Window->GetPositionInScreen().X > 0 ? Window->GetPositionInScreen().X : 0);
			uint32 TotalY = FMath::Min(Texture->GetSizeXY().Y, Window->GetSizeInScreen().IntPoint().Y) + (Window->GetPositionInScreen().Y > 0 ? Window->GetPositionInScreen().Y : 0);

			// Check if the location + size of our texture will be outside the bounds of the compositedframe
			if (CompositedX < TotalX || CompositedY < TotalY)
			{
				// If so, create a new compositedframe large enough to accomodate our new texture + its position
				// and render the contents of the old compositedframe to the new one
				uint32 SizeX = (TotalX > CompositedX ? TotalX : CompositedX);
				uint32 SizeY = (TotalY > CompositedY ? TotalY : CompositedY);

				StagingTexture = UE::PixelStreaming::CreateRHITexture(SizeX, SizeY);
				FRHICopyTextureInfo CopyInfo;
				CopyInfo.Size.X = CompositedFrame->GetSizeXY().X;
				CopyInfo.Size.Y = CompositedFrame->GetSizeXY().Y;
				CopyInfo.Size.Z = 1;
				CopyInfo.DestPosition.X = 0;
				CopyInfo.DestPosition.Y = 0;
				CopyInfo.DestPosition.Z = 0;
				// source and dest are the same. simple copy
				RHICmdList.Transition(FRHITransitionInfo(CompositedFrame, ERHIAccess::Unknown, ERHIAccess::CopySrc));
				RHICmdList.Transition(FRHITransitionInfo(StagingTexture, ERHIAccess::Unknown, ERHIAccess::CopyDest));
				RHICmdList.CopyTexture(CompositedFrame, StagingTexture, CopyInfo);
				// Update our composited frame with the new, larger texture for later use
				CompositedFrame = StagingTexture;
			}

			// Copy our new texture to the compositedframe texture
			FRHICopyTextureInfo CopyInfo;
			// There is only ever one tooltip and as such UE keeps the same texture for each and just rerenders the content
			// this can lead to small tooltips having a large texture from a previously displayed long tooltip
			// so we use the tooltips window size which is guaranteed to be correct
			CopyInfo.Size.X = FMath::Min(Texture->GetSizeXY().X, Window->GetSizeInScreen().IntPoint().X);
			CopyInfo.Size.Y = FMath::Min(Texture->GetSizeXY().Y, Window->GetSizeInScreen().IntPoint().Y);
			CopyInfo.Size.Z = 1;
			// Sometimes a windows position in screen is negative (eg the main window has a position of -8,-8).
			// This causes issues when copying the texture so we'll just ignore negative values
			CopyInfo.DestPosition.X = Window->GetPositionInScreen().X > 0 ? Window->GetPositionInScreen().X : 0;
			CopyInfo.DestPosition.Y = Window->GetPositionInScreen().Y > 0 ? Window->GetPositionInScreen().Y : 0;
			CopyInfo.DestPosition.Z = 0;
			// source and dest are the same. simple copy
			RHICmdList.Transition(FRHITransitionInfo(Texture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.Transition(FRHITransitionInfo(CompositedFrame, ERHIAccess::Unknown, ERHIAccess::CopyDest));
			RHICmdList.CopyTexture(Texture, CompositedFrame, CopyInfo);
		}

		// Our composition is complete, send it to along the pipeline
		OnFrame(FPixelCaptureInputFrameRHI(CompositedFrame));
		// Update the default streamer to let it know out compositedframe size. This way it can correctly scale input from the browser
		*CompositedFrameSize.Get() = CompositedFrame->GetSizeXY();
		IPixelStreamingModule& Module = IPixelStreamingModule::Get();
		Module.GetStreamer(Module.GetDefaultStreamerID())->SetTargetScreenSize(CompositedFrameSize);
	}
	TopLevelWindowsCriticalSection.Unlock();
}
