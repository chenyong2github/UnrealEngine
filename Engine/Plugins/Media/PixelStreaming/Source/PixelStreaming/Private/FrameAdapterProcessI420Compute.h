// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameAdapterProcess.h"

namespace UE::PixelStreaming
{
	class FAdaptedVideoFrameLayerI420;
	
	class FFrameAdapterProcessI420Compute : public FPixelStreamingFrameAdapterProcess
	{
	public:
		FFrameAdapterProcessI420Compute(float InScale);
		virtual ~FFrameAdapterProcessI420Compute();

	protected:
		virtual void Initialize(int32 SourceWidth, int32 SourceHeight) override;
		virtual void OnSourceResolutionChanged(int32 OldWidth, int32 OldHeight, int32 NewWidth, int32 NewHeight) override;
		virtual TSharedPtr<IPixelStreamingAdaptedVideoFrameLayer> CreateOutputBuffer(int32 SourceWidth, int32 SourceHeight) override;
		virtual void BeginProcess(const FPixelStreamingSourceFrame& SourceFrame) override;

	private:
		float Scale = 1.0f;

		// dimensions of the texures
		FIntPoint PlaneYDimensions;
		FIntPoint PlaneUVDimensions;

		// used as targets for the compute shader
		FTextureRHIRef TextureY;
		FTextureRHIRef TextureU;
		FTextureRHIRef TextureV;

		// the UAVs of the targets
		FUnorderedAccessViewRHIRef TextureYUAV;
		FUnorderedAccessViewRHIRef TextureUUAV;
		FUnorderedAccessViewRHIRef TextureVUAV;

		// cpu readable copies of the targets above
		FTextureRHIRef StagingTextureY;
		FTextureRHIRef StagingTextureU;
		FTextureRHIRef StagingTextureV;

		// memory mapped pointers of the staging textures
		void* MappedY = nullptr;
		void* MappedU = nullptr;
		void* MappedV = nullptr;

		int32 YStride = 0;
		int32 UStride = 0;
		int32 VStride = 0;

		FAdaptedVideoFrameLayerI420* CurrentOuputBuffer = nullptr;

		void OnRHIStageComplete();
		void CleanUp();
	};
} // namespace UE::PixelStreaming
