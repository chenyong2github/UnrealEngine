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
#include "PixelStreamingUtils.h"

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
		RegisterRemoteResolutionCommandHandler();
	}

	Capture = nullptr;
	if (Streamer.IsValid())
	{
		Capture = NewObject<UPixelStreamingMediaCapture>();
		Capture->SetMediaOutput(this);
	}

	if (!VideoInput)
	{
		VideoInput = MakeShared<FPixelStreamingVideoInputRHI>();
	}

	Capture->SetVideoInput(VideoInput);

	return Capture;
}

void UPixelStreamingMediaOutput::RegisterRemoteResolutionCommandHandler()
{
	// Override resolution command as we this to set the output provider override resolution
	TSharedPtr<IPixelStreamingInputHandler> InputHandler = Streamer->GetInputHandler().Pin();
	if(InputHandler)
	{
		InputHandler->SetCommandHandler(TEXT("Resolution.Width"), [this](FString Descriptor, FString WidthString){
			bool bSuccess = false;
			FString HeightString;
			UE::PixelStreaming::ExtractJsonFromDescriptor(Descriptor, TEXT("Resolution.Height"), HeightString, bSuccess);
			if (bSuccess)
			{
				int Width = FCString::Atoi(*WidthString);
				int Height = FCString::Atoi(*HeightString);
				if (Width < 1 || Height < 1)
				{
					return;
				}

				RemoteResolutionChangedEvent.Broadcast(FIntPoint(Width, Height));
			}
		});
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