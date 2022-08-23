// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingVideoInput.h"
#include "Delegates/IDelegateInstance.h"
#include "UnrealClient.h"

namespace UE::PixelStreaming
{
	/*
	 * Use this if you want to send the UE primary scene viewport as video input - will only work in editor.
	 */
	class PIXELSTREAMING_API FPixelStreamingVideoInputViewport : public FPixelStreamingVideoInput
	{
	public:
		static TSharedPtr<FPixelStreamingVideoInputViewport> Create();
		virtual ~FPixelStreamingVideoInputViewport();

	protected:
		virtual TSharedPtr<FPixelCaptureCapturer> CreateCapturer(int32 FinalFormat, float FinalScale) override;

	private:
		FPixelStreamingVideoInputViewport() = default;

		void OnViewportRendered(FViewport* Viewport);

		FDelegateHandle DelegateHandle;

		FName TargetViewportType = FName(FString(TEXT("SceneViewport")));
	};
} // namespace UE::PixelStreaming
