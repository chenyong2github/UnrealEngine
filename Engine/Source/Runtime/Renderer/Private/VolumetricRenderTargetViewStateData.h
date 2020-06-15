// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VolumetricRenderTargetViewStatedata.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"

class FRDGBuilder;

class FVolumetricRenderTargetViewStateData
{

public:

	FVolumetricRenderTargetViewStateData();
	~FVolumetricRenderTargetViewStateData();

	void Initialise(FIntPoint& ViewRectResolutionIn);

	FRDGTextureRef GetOrCreateVolumetricTracingRT(FRDGBuilder& GraphBuilder);
	FRDGTextureRef GetOrCreateVolumetricTracingRTDepth(FRDGBuilder& GraphBuilder);
	void ExtractToVolumetricTracingRT(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGPixelSubSetRT);
	void ExtractToVolumetricTracingRTDepth(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGPixelSubSetRTDepth);

	FRDGTextureRef GetOrCreateDstVolumetricReconstructRT(FRDGBuilder& GraphBuilder);
	FRDGTextureRef GetOrCreateDstVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder);
	void ExtractDstVolumetricReconstructRT(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGFullResRT);
	void ExtractDstVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder, FRDGTextureRef RDGFullResRT);

	FRDGTextureRef GetOrCreateSrcVolumetricReconstructRT(FRDGBuilder& GraphBuilder);
	FRDGTextureRef GetOrCreateSrcVolumetricReconstructRTDepth(FRDGBuilder& GraphBuilder);

	bool GetHistoryValid() const { return bHistoryValid; }
	bool GetVolumetricTracingRTValid() const { return bVolumetricTracingRTValid && bVolumetricTracingRTDepthValid; }
	const FIntPoint& GetCurrentVolumetricReconstructRTResolution() const { return VolumetricReconstructRTResolution; }
	const FIntPoint& GetCurrentVolumetricTracingRTResolution() const { return VolumetricTracingRTResolution; }
	const FIntPoint& GetCurrentTracingPixelOffset() const { return CurrentPixelOffset; }
	const uint32 GetNoiseFrameIndexModPattern() const { return NoiseFrameIndexModPattern; }

	const uint32 GetVolumetricReconstructRTDownsampleFactor() const { return VolumetricReconstructRTDownsampleFactor; }
	const uint32 GetVolumetricTracingRTDownsampleFactor() const { return VolumetricTracingRTDownsampleFactor; }

	FUintVector4 GetTracingToFullResResolutionScaleBias() const;


private:

	uint32 VolumetricReconstructRTDownsampleFactor;
	uint32 VolumetricTracingRTDownsampleFactor;

	uint32 CurrentRT;
	bool bFirstTimeUsed;
	bool bHistoryValid;
	bool bVolumetricTracingRTValid;
	bool bVolumetricTracingRTDepthValid;

	int32 FrameId;
	uint32 NoiseFrameIndex;	// This is only incremented once all Volumetric render target samples have been iterated
	uint32 NoiseFrameIndexModPattern;
	FIntPoint CurrentPixelOffset;

	FIntPoint FullResolution;
	FIntPoint VolumetricReconstructRTResolution;
	FIntPoint VolumetricTracingRTResolution;

	static constexpr uint32 kRenderTargetCount = 2;
	TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRT[kRenderTargetCount];
	TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRTDepth[kRenderTargetCount];

	TRefCountPtr<IPooledRenderTarget> VolumetricTracingRT;
	TRefCountPtr<IPooledRenderTarget> VolumetricTracingRTDepth;
};





