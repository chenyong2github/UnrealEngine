// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FrameAdapter.h"
#include "PixelStreamingFrameAdapterProcess.h"
#include "PixelStreamingAdaptedOutputFrameH264.h"
#include "RHI.h"

namespace UE::PixelStreaming
{
	class FFrameAdapterProcessRHIToH264 : public FPixelStreamingFrameAdapterProcess
	{
	public:
		FFrameAdapterProcessRHIToH264(float InScale);
		virtual ~FFrameAdapterProcessRHIToH264() = default;

	protected:
		virtual FString GetProcessName() const override { return "RHIToH264"; }
		virtual TSharedPtr<IPixelStreamingAdaptedOutputFrame> CreateOutputBuffer(int32 InputWidth, int32 InputHeight) override;
		virtual void BeginProcess(const IPixelStreamingInputFrame& InputFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer) override;

	private:
		float Scale = 1.0f;

		FGPUFenceRHIRef Fence;

		void CheckComplete();
		void OnRHIStageComplete();
	};
} // namespace UE::PixelStreaming
