// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoInputBackBuffer.h"
#include "Framework/Application/SlateApplication.h"
#include "PixelStreamingSourceFrame.h"
#include "Settings.h"

namespace UE::PixelStreaming
{
	FVideoInputBackBuffer::FVideoInputBackBuffer()
	{
		DelegateHandle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddRaw(this, &FVideoInputBackBuffer::OnBackBufferReady);
	}

	FVideoInputBackBuffer::~FVideoInputBackBuffer()
	{
		if (!IsEngineExitRequested())
		{
			FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(DelegateHandle);
		}
	}

	void FVideoInputBackBuffer::OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
	{
		OnFrame.Broadcast(FPixelStreamingSourceFrame(FrameBuffer));
	}
} // namespace UE::PixelStreaming
