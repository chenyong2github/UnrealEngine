// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingVideoInput.h"
#include "Widgets/SWindow.h"
#include "RHI.h"

namespace UE::PixelStreaming
{
	class FVideoInputBackBuffer : public FPixelStreamingVideoInput
	{
	public:
		FVideoInputBackBuffer();
		virtual ~FVideoInputBackBuffer();

	private:
		FDelegateHandle DelegateHandle;

		void OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer);
	};
} // namespace UE::PixelStreaming
