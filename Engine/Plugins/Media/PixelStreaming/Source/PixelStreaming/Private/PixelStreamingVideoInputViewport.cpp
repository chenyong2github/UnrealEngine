// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVideoInputViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "PixelStreamingInputFrameRHI.h"
#include "FrameAdapterProcessRHIToH264.h"
#include "FrameAdapterProcessRHIToI420CPU.h"
#include "FrameAdapterProcessRHIToI420Compute.h"
#include "Settings.h"
#include "Utils.h"
#include "Engine/GameViewportClient.h"
#include "PixelStreamingModule.h"
#include "UnrealClient.h"
#include "RenderingThread.h"

namespace UE::PixelStreaming
{
	TSharedPtr<FPixelStreamingVideoInputViewport> FPixelStreamingVideoInputViewport::Create()
	{
		TSharedPtr<FPixelStreamingVideoInputViewport> NewInput = TSharedPtr<FPixelStreamingVideoInputViewport>(new FPixelStreamingVideoInputViewport());
		TWeakPtr<FPixelStreamingVideoInputViewport> WeakInput = NewInput;

		// Set up the callback on the game thread since FSlateApplication::Get() can only be used there
		UE::PixelStreaming::DoOnGameThread([WeakInput]() {
			if (TSharedPtr<FPixelStreamingVideoInputViewport> Input = WeakInput.Pin())
			{
				Input->DelegateHandle = UGameViewportClient::OnViewportRendered().AddSP(Input.ToSharedRef(), &FPixelStreamingVideoInputViewport::OnViewportRendered);
			}
		});

		return NewInput;
	}

	FPixelStreamingVideoInputViewport::~FPixelStreamingVideoInputViewport()
	{
		if (!IsEngineExitRequested())
		{
			UE::PixelStreaming::DoOnGameThread([HandleCopy = DelegateHandle]() {
				UGameViewportClient::OnViewportRendered().Remove(HandleCopy);
			});
		}
	}

	void FPixelStreamingVideoInputViewport::OnViewportRendered(FViewport* InViewport)
	{
		TSharedPtr<IPixelStreamingStreamer> Streamer = IPixelStreamingModule::Get().GetStreamer(UE::PixelStreaming::Settings::GetDefaultStreamerID());
		if(!Streamer.IsValid() || !Streamer->IsStreaming())
		{
			return;	
		}
		
		FSceneViewport* TargetScene = Streamer->GetTargetViewport();
		if(TargetScene == nullptr)
		{
			return;
		}
		
		if(InViewport != TargetScene->GetViewport())
		{
			return;
		}
		
		const FTextureRHIRef& FrameBuffer = InViewport->GetRenderTargetTexture();
		if(!FrameBuffer) 
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(StreamViewportTextureCommand)([&, FrameBuffer](FRHICommandList& RHICmdList)
		{	
			OnFrame(FPixelStreamingInputFrameRHI(FrameBuffer));
		});
	}

}