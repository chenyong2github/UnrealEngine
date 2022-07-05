// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingFrameAdapterProcess.h"
#include "PixelStreamingAdaptedOutputFrameI420.h"
#include "RHI.h"

namespace UE::PixelStreaming
{
	class FFrameAdapterProcessRHIToI420Compute : public FPixelStreamingFrameAdapterProcess
	{
	public:
		FFrameAdapterProcessRHIToI420Compute(float InScale);
		virtual ~FFrameAdapterProcessRHIToI420Compute();

	protected:
		virtual FString GetProcessName() const override { return "RHIToI420Compute"; }
		virtual void Initialize(int32 InputWidth, int32 InputHeight) override;
		virtual TSharedPtr<IPixelStreamingAdaptedOutputFrame> CreateOutputBuffer(int32 InputWidth, int32 InputHeight) override;
		virtual void BeginProcess(const IPixelStreamingInputFrame& InputFrame, TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer) override;

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

		void OnRHIStageComplete(TSharedPtr<IPixelStreamingAdaptedOutputFrame> OutputBuffer);
		void CleanUp();
	};
} // namespace UE::PixelStreaming
