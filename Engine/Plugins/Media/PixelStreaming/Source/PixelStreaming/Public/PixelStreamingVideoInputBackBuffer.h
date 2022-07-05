// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingVideoInput.h"
#include "Widgets/SWindow.h"
#include "RHI.h"
#include "Delegates/IDelegateInstance.h"


namespace UE::PixelStreaming
{
	class PIXELSTREAMING_API FPixelStreamingVideoInputBackBuffer : public FPixelStreamingVideoInput
	{
	public:
		static TSharedPtr<FPixelStreamingVideoInputBackBuffer> Create();
		virtual ~FPixelStreamingVideoInputBackBuffer();

	private:
		FPixelStreamingVideoInputBackBuffer() = default;
		
		void OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer);

		FDelegateHandle DelegateHandle;
	};
}
