// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LightmapTilePool.h"
#include "RHIGPUReadback.h"

namespace GPULightmass
{

class FSceneRenderState;

struct FLightmapTileRequest
{
	FLightmapRenderStateRef RenderState;
	FTileVirtualCoordinates VirtualCoordinates;

	FLightmapTileRequest(
		FLightmapRenderStateRef RenderState,
		FTileVirtualCoordinates VirtualCoordinates)
		: RenderState(RenderState)
		, VirtualCoordinates(VirtualCoordinates)
	{}

	~FLightmapTileRequest() {} // this deletes the move constructor while keeps copy constructors

	FIntPoint OutputPhysicalCoordinates[8];
	IPooledRenderTarget* OutputRenderTargets[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	uint32 TileAddressInWorkingSet = ~0u;
	uint32 TileAddressInScratch = ~0u;
};

struct FLightmapReadbackGroup
{
	int32 Revision;
	int32 GPUIndex = 0;
	TUniquePtr<FLightmapTilePoolGPU> ReadbackTilePoolGPU;
	TArray<FLightmapTileRequest> ConvergedTileRequests;
	TUniquePtr<FRHIGPUTextureReadback> StagingHQLayer0Readback;
	TUniquePtr<FRHIGPUTextureReadback> StagingHQLayer1Readback;
	TUniquePtr<FRHIGPUTextureReadback> StagingShadowMaskReadback;
};

class FLightmapRenderer : public IVirtualTextureFinalizer
{
public:
	FLightmapRenderer(FSceneRenderState* InScene);

	void AddRequest(FLightmapTileRequest TileRequest);

	virtual void Finalize(FRHICommandListImmediate& RHICmdList) override;

	virtual ~FLightmapRenderer() {}

	void BackgroundTick();

	int32 CurrentRevision = 0;
	int32 FrameNumber = 0;

	bool bUseFirstBounceRayGuiding = false;
	int32 NumFirstBounceRayGuidingTrialSamples = 0;

private:
	int32 LastInvalidationFrame = 0;
	int32 Mip0WorkDoneLastFrame = 0;
	bool bIsExiting = false;
	bool bInsideBackgroundTick = false;
	bool bWasRunningAtFullSpeed = true;

	FSceneRenderState* Scene;

	TArray<FLightmapTileRequest> PendingTileRequests;

	FLightmapTilePoolGPU LightmapTilePoolGPU;

	TUniquePtr<FLightmapTilePoolGPU> ScratchTilePoolGPU;

	TArray<FLightmapReadbackGroup> OngoingReadbacks;

	TUniquePtr<FLightmapTilePoolGPU> UploadTilePoolGPU;
};

}

extern int32 GGPULightmassSamplesPerTexel;
extern int32 GGPULightmassShadowSamplesPerTexel;
