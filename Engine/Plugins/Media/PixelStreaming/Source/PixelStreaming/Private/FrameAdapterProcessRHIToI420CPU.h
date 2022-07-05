// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameAdapterProcess.h"
#include "PixelStreamingAdaptedOutputFrameI420.h"
#include "RHI.h"

namespace UE::PixelStreaming
{
	class FFrameAdapterProcessRHIToI420CPU : public FPixelStreamingFrameAdapterProcess
	{
	public:
		FFrameAdapterProcessRHIToI420CPU(float InScale);
		virtual ~FFrameAdapterProcessRHIToI420CPU();

	protected:
		virtual FString GetProcessName() const override { return "RHIToI420CPU"; }
		virtual void Initialize(int32 InputWidth, int32 InputHeight) override;
		virtual TSharedPtr<IPixelStreamingAdaptedOutputFrame> CreateOutputBuffer(int32 InputWidth, int32 InputHeight) override;
		virtual void BeginProcess(const IPixelStreamingInputFrame& InputFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer) override;

	private:
		float Scale = 1.0f;

		FTextureRHIRef StagingTexture;
		FTextureRHIRef ReadbackTexture;
		void* ResultsBuffer = nullptr;
		int32 MappedStride = 0;

		void OnRHIStageComplete(TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer);
		void CleanUp();
	};
} // namespace UE::PixelStreaming
