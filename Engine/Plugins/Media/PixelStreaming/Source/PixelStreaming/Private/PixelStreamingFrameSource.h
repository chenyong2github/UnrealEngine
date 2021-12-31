// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RHI.h"
#include "Containers/TripleBuffer.h"
#include "HAL/ThreadSafeBool.h"

class FPixelStreamingLayerFrameSource;

// handles the partitioning out of the input frame to the multiple layers
// can be queried for a layers frame source
class FPixelStreamingFrameSource
{
public:
	FPixelStreamingFrameSource();
	~FPixelStreamingFrameSource();

	bool IsAvailable() const { return bAvailable; }

	// input
	void OnFrameReady(const FTexture2DRHIRef& FrameBuffer);

	// output
	FPixelStreamingLayerFrameSource* GetLayerFrameSource(int LayerIndex);

	int GetSourceWidth() const;
	int GetSourceHeight() const;
	int GetNumLayers() const;

private:
	TArray<TUniquePtr<FPixelStreamingLayerFrameSource>> LayerSources;

	bool bAvailable = false;
	int32 NextFrameId = 1;
};

// handles the scaling of back buffer frames
// allows the getting of the most recent frame for a singular layer
class FPixelStreamingLayerFrameSource
{
public:
	FPixelStreamingLayerFrameSource(float InScale);
	~FPixelStreamingLayerFrameSource();

	void OnFrameReady(const FTexture2DRHIRef& FrameBuffer);

	int GetSourceWidth() const { return SourceWidth; }
	int GetSourceHeight() const { return SourceHeight; }

	FTexture2DRHIRef GetFrame();

public:
	const float FrameScale;

private:
	int SourceWidth = 0;
	int SourceHeight = 0;

	bool bInitialized = false;
	void Initialize(int Width, int Height);

	struct FCaptureFrame
	{
		FTexture2DRHIRef Texture;
		FGPUFenceRHIRef Fence;
		bool bAvailable = true;
		uint64 PreWaitingOnCopy;
	};

	// triple buffer setup with queued write buffers (since we have to wait for RHI copy)
	FCriticalSection CriticalSection;
	bool bWriteParity = true;
	FCaptureFrame WriteBuffers[2];
	FTexture2DRHIRef TempBuffer;
	FTexture2DRHIRef ReadBuffer;
	FThreadSafeBool bIsTempDirty;
};
