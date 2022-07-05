// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVideoInput.h"
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
	TSharedPtr<FPixelStreamingVideoInput> FPixelStreamingVideoInput::Create()
	{
		TSharedPtr<FPixelStreamingVideoInput> NewInput = TSharedPtr<FPixelStreamingVideoInput>(new FPixelStreamingVideoInput());
		TWeakPtr<FPixelStreamingVideoInput> WeakInput = NewInput;

		return NewInput;
	}

	FPixelStreamingVideoInput::~FPixelStreamingVideoInput()
	{

	}

	TSharedPtr<FPixelStreamingFrameAdapterProcess> FPixelStreamingVideoInput::CreateAdaptProcess(EPixelStreamingFrameBufferFormat FinalFormat, float FinalScale)
	{
		switch (FinalFormat)
		{
			case EPixelStreamingFrameBufferFormat::RHITexture:
				return MakeShared<UE::PixelStreaming::FFrameAdapterProcessRHIToH264>(FinalScale);
			case EPixelStreamingFrameBufferFormat::IYUV420:
				if (UE::PixelStreaming::Settings::CVarPixelStreamingVPXUseCompute.GetValueOnAnyThread())
				{
					return MakeShared<UE::PixelStreaming::FFrameAdapterProcessRHIToI420Compute>(FinalScale);
				}
				else
				{
					return MakeShared<UE::PixelStreaming::FFrameAdapterProcessRHIToI420CPU>(FinalScale);
				}
			default:
				return nullptr;
		}
	}

	void FPixelStreamingVideoInput::OnFrame(const IPixelStreamingInputFrame& InputFrame)
	{
		if (LastFrameWidth != -1 && LastFrameHeight != -1)
		{
			if (InputFrame.GetWidth() != LastFrameWidth || InputFrame.GetHeight() != LastFrameHeight)
			{
				OnResolutionChanged.Broadcast(InputFrame.GetWidth(), InputFrame.GetHeight());
			}
		}

		OnFrameReady.Broadcast(InputFrame);
		LastFrameWidth = InputFrame.GetWidth();
		LastFrameHeight = InputFrame.GetHeight();
	}
}