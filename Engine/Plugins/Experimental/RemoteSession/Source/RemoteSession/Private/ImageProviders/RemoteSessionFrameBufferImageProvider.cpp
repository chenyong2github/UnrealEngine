// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ImageProviders/RemoteSessionFrameBufferImageProvider.h"
#include "Channels/RemoteSessionFrameBufferChannel.h"
#include "RemoteSession.h"
#include "HAL/IConsoleManager.h"
#include "FrameGrabber.h"
#include "Async/Async.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "Modules/ModuleManager.h"

DECLARE_CYCLE_STAT(TEXT("RSFrameBufferCap"), STAT_FrameBufferCapture, STATGROUP_Game);
DECLARE_CYCLE_STAT(TEXT("RSImageCompression"), STAT_ImageCompression, STATGROUP_Game);

static int32 FramerateMasterSetting = 0;
static FAutoConsoleVariableRef CVarFramerateOverride(
	TEXT("remote.framerate"), FramerateMasterSetting,
	TEXT("Sets framerate"),
	ECVF_Default);

static int32 FrameGrabberResX = 0;
static FAutoConsoleVariableRef CVarResXOverride(
	TEXT("remote.framegrabber.resx"), FrameGrabberResX,
	TEXT("Sets the desired X resolution"),
	ECVF_Default);

static int32 FrameGrabberResY = 0;
static FAutoConsoleVariableRef CVarResYOverride(
	TEXT("remote.framegrabber.resy"), FrameGrabberResY,
	TEXT("Sets the desired Y resolution"),
	ECVF_Default);

FRemoteSessionFrameBufferImageProvider::FRemoteSessionFrameBufferImageProvider(TWeakPtr<FRemoteSessionImageChannel> InOwner)
{
	ImageChannel = InOwner;
	LastSentImageTime = 0.0;
	ViewportResized = false;
}

FRemoteSessionFrameBufferImageProvider::~FRemoteSessionFrameBufferImageProvider()
{
	ReleaseFrameGrabber();
}

void FRemoteSessionFrameBufferImageProvider::ReleaseFrameGrabber()
{
	if (FrameGrabber.IsValid())
	{
		FrameGrabber->Shutdown();
		FrameGrabber = nullptr;
	}
}

void FRemoteSessionFrameBufferImageProvider::SetCaptureFrameRate(int32 InFramerate)
{
	// Set our framerate and quality cvars, if the user hasn't modified them
	if (FramerateMasterSetting == 0)
	{
		CVarFramerateOverride->Set(InFramerate);
	}
}

void FRemoteSessionFrameBufferImageProvider::SetCaptureViewport(TSharedRef<FSceneViewport> Viewport)
{
	SceneViewport = Viewport;

	CreateFrameGrabber(Viewport);

	// set the listener for the window resize event
	Viewport->SetOnSceneViewportResizeDel(FOnSceneViewportResize::CreateRaw(this, &FRemoteSessionFrameBufferImageProvider::OnViewportResized));
}

void FRemoteSessionFrameBufferImageProvider::CreateFrameGrabber(TSharedRef<FSceneViewport> Viewport)
{
	ReleaseFrameGrabber();

	// For times when we want a specific resolution
	FIntPoint FrameGrabberSize = SceneViewport->GetSize();
	if (FrameGrabberResX > 0)
	{
		FrameGrabberSize.X = FrameGrabberResX;
	}
	if (FrameGrabberResY > 0)
	{
		FrameGrabberSize.Y = FrameGrabberResY;
	}

	FrameGrabber = MakeShared<FFrameGrabber>(Viewport, FrameGrabberSize);
	FrameGrabber->StartCapturingFrames();
}

void FRemoteSessionFrameBufferImageProvider::Tick(const float InDeltaTime)
{
	if (FrameGrabber.IsValid())
	{
		if (ViewportResized)
		{
			CreateFrameGrabber(SceneViewport.ToSharedRef());
			ViewportResized = false;
		}
		SCOPE_CYCLE_COUNTER(STAT_FrameBufferCapture);

		FrameGrabber->CaptureThisFrame(FFramePayloadPtr());

		TArray<FCapturedFrameData> Frames = FrameGrabber->GetCapturedFrames();

		if (Frames.Num())
		{
			const double ElapsedImageTimeMS = (FPlatformTime::Seconds() - LastSentImageTime) * 1000;
			const int32 DesiredFrameTimeMS = 1000 / FramerateMasterSetting;

			// Encoding/decoding can take longer than a frame, so skip if we're still processing the previous frame
			if (NumDecodingTasks.GetValue() == 0 && ElapsedImageTimeMS >= DesiredFrameTimeMS)
			{
				NumDecodingTasks.Increment();

				FCapturedFrameData& LastFrame = Frames.Last();

				TArray<FColor>* ColorData = new TArray<FColor>(MoveTemp(LastFrame.ColorBuffer));

				FIntPoint Size = LastFrame.BufferSize;

				AsyncTask(ENamedThreads::AnyBackgroundHiPriTask, [this, Size, ColorData]()
				{
					SCOPE_CYCLE_COUNTER(STAT_ImageCompression);

					if (TSharedPtr<FRemoteSessionImageChannel> ImageChannelPinned = ImageChannel.Pin())
					{
						for (FColor& Color : *ColorData)
						{
							Color.A = 255;
						}

						ImageChannelPinned->SendRawImageToClients(Size.X, Size.Y, *ColorData);
					}

					delete ColorData;

					NumDecodingTasks.Decrement();
				});

				LastSentImageTime = FPlatformTime::Seconds();
			}
		}
	}
}

void FRemoteSessionFrameBufferImageProvider::OnViewportResized(FVector2D NewSize)
{
	ViewportResized = true;
}
