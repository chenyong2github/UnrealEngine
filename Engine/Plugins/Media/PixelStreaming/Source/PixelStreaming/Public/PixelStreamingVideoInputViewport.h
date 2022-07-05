// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingVideoInput.h"
#include "Delegates/IDelegateInstance.h"
#include "UnrealClient.h"

namespace UE::PixelStreaming
{
	class PIXELSTREAMING_API FPixelStreamingVideoInputViewport : public FPixelStreamingVideoInput
	{
	public:
		static TSharedPtr<FPixelStreamingVideoInputViewport> Create();
		virtual ~FPixelStreamingVideoInputViewport();
		
	private:
		FPixelStreamingVideoInputViewport() = default;
		
		void OnViewportRendered(FViewport* Viewport);

		FDelegateHandle DelegateHandle;
	};
}
