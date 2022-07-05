// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingVideoInput.h"
#include "Delegates/IDelegateInstance.h"
#include "UnrealClient.h"

namespace UE::PixelStreaming
{
	class PIXELSTREAMING_API FPixelStreamingVideoInput : public IPixelStreamingVideoInput
	{
	public:
		FPixelStreamingVideoInput() = default;
		
		static TSharedPtr<FPixelStreamingVideoInput> Create();
		virtual ~FPixelStreamingVideoInput();

		void OnFrame(const IPixelStreamingInputFrame&) override;

	protected:
		virtual TSharedPtr<FPixelStreamingFrameAdapterProcess> CreateAdaptProcess(EPixelStreamingFrameBufferFormat FinalFormat, float FinalScale) override;

	private:
		int32 LastFrameWidth = -1;
		int32 LastFrameHeight = -1;
	};
}
