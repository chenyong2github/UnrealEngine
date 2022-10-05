// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingMediaOutput.h"
#include "PixelStreamingMediaCapture.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingPlayerId.h"
#include "IPixelStreamingStreamer.h"
#include "CineCameraComponent.h"
#include "PixelStreamingEditorModule.h"
#include "PixelStreamingEditorUtils.h"
#include "PixelStreamingVideoInputRHI.h"

void UPixelStreamingMediaOutput::BeginDestroy()
{
	StopStreaming();
	Streamer = nullptr;
	Super::BeginDestroy();
}

UMediaCapture* UPixelStreamingMediaOutput::CreateMediaCaptureImpl()
{
	if (!Streamer.IsValid())
	{
		IPixelStreamingModule& Module = FModuleManager::LoadModuleChecked<IPixelStreamingModule>("PixelStreaming");
		Streamer = Module.GetStreamer(Module.GetDefaultStreamerID());
	}

	Capture = nullptr;
	if (Streamer.IsValid())
	{
		Capture = NewObject<UPixelStreamingMediaCapture>();
		Capture->SetMediaOutput(this);
		Capture->OnCaptureViewportInitialized.AddUObject(this, &UPixelStreamingMediaOutput::OnCaptureViewportInitialized);
	}

	if (!VideoInput)
	{
		VideoInput = MakeShared<FPixelStreamingVideoInputRHI>();
	}

	Capture->SetVideoInput(VideoInput);

	return Capture;
}

void UPixelStreamingMediaOutput::OnCaptureViewportInitialized()
{
	if(Streamer)
	{
		Streamer->SetTargetViewport(Capture->GetViewport()->GetViewportWidget());
		Streamer->SetTargetWindow(Capture->GetViewport()->FindWindow());
	}
}

void UPixelStreamingMediaOutput::StartStreaming()
{
	if (Streamer)
	{
		FPixelStreamingEditorModule::GetModule()->SetStreamType(UE::EditorPixelStreaming::EStreamTypes::VCam);
		
		// Only update streamer's video input if we don't have one or it is different than the one we already have.
		if(VideoInput.IsValid())
		{
			TSharedPtr<FPixelStreamingVideoInput> StreamerVideoInput = Streamer->GetVideoInput().Pin();
			if(!StreamerVideoInput.IsValid() || StreamerVideoInput != VideoInput)
			{
				Streamer->SetVideoInput(VideoInput);
			}
		}

		if (!Streamer->IsStreaming())
		{
			Streamer->StartStreaming();
		}
	}
}

void UPixelStreamingMediaOutput::StopStreaming()
{
	if (Streamer)
	{
		Streamer->StopStreaming();
		Streamer->SetTargetViewport(nullptr);
		Streamer->SetTargetWindow(nullptr);
	}
}

void UPixelStreamingMediaOutput::SetSignallingServerURL(FString InURL)
{
	SignallingServerURL = InURL;
}

void UPixelStreamingMediaOutput::SetSignallingStreamID(FString InStreamID)
{
	StreamID = InStreamID;
}