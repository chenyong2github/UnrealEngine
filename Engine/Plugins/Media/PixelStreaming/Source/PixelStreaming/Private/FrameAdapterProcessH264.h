// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameAdapterProcess.h"

namespace UE::PixelStreaming
{
	class FAdaptedVideoFrameLayerH264;
	
	class FFrameAdapterProcessH264 : public FPixelStreamingFrameAdapterProcess
	{
	public:
		FFrameAdapterProcessH264(float InScale);
		virtual ~FFrameAdapterProcessH264() = default;

	protected:
		virtual TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight) override;
		virtual void BeginProcess(const FPixelStreamingSourceFrame& SourceFrame) override;

	private:
		float Scale = 1.0f;

		FGPUFenceRHIRef Fence;
		FAdaptedVideoFrameLayerH264* CurrentOuputBuffer = nullptr;

		void CheckComplete();
		void OnRHIStageComplete();
	};
} // namespace UE::PixelStreaming
