// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingVideoInput.h"
#include "Widgets/SWindow.h"
#include "RHI.h"
#include "Delegates/IDelegateInstance.h"
#include "Widgets/SWindow.h"
#include "GenericPlatform/GenericWindowDefinition.h"

namespace UE::PixelStreaming
{
	/*
	 * Use this if you want to send the UE backbuffer as video input.
	 */
	class PIXELSTREAMING_API FPixelStreamingVideoInputBackBufferComposited : public FPixelStreamingVideoInput
	{
	public:
		static TSharedPtr<FPixelStreamingVideoInputBackBufferComposited> Create();
		virtual ~FPixelStreamingVideoInputBackBufferComposited();

	private:
		FPixelStreamingVideoInputBackBufferComposited();

		void OnBackBufferReady(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer);
		virtual TSharedPtr<FPixelCaptureCapturer> CreateCapturer(int32 FinalFormat, float FinalScale) override;

		FDelegateHandle DelegateHandle;

		TArray<TSharedRef<SWindow>> TopLevelWindows;
		TMap<SWindow*, FTextureRHIRef> TopLevelWindowTextures;

		FCriticalSection TopLevelWindowsCriticalSection;
		FTexture2DRHIRef CompositedFrame;
		TSharedPtr<FIntPoint> CompositedFrameSize;
		bool bRecreateTexture = false;
		FIntPoint DefaultSize = FIntPoint(1, 1);
	};
} // namespace UE::PixelStreaming
