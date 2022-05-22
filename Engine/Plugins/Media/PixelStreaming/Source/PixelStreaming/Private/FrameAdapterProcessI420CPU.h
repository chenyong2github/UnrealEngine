// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameAdapterProcess.h"

namespace UE::PixelStreaming
{
	class FAdaptedVideoFrameLayerI420;
	
	class FFrameAdapterProcessI420CPU : public FPixelStreamingFrameAdapterProcess
	{
	public:
		FFrameAdapterProcessI420CPU(float InScale);
		virtual ~FFrameAdapterProcessI420CPU();

	protected:
		virtual void Initialize(int32 SourceWidth, int32 SourceHeight) override;
		virtual void OnSourceResolutionChanged(int32 OldWidth, int32 OldHeight, int32 NewWidth, int32 NewHeight) override;
		virtual TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight) override;
		virtual void BeginProcess(const FPixelStreamingSourceFrame& SourceFrame) override;

	private:
		float Scale = 1.0f;

		FTextureRHIRef StagingTexture;
		FTextureRHIRef ReadbackTexture;
		void* ResultsBuffer = nullptr;
		int32 MappedStride = 0;

		FAdaptedVideoFrameLayerI420* CurrentOuputBuffer = nullptr;

		void OnRHIStageComplete();
		void CleanUp();
	};
} // namespace UE::PixelStreaming
