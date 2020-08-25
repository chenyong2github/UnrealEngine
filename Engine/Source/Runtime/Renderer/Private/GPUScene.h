// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RenderResource.h"
#include "RendererInterface.h"
#include "PrimitiveUniformShaderParameters.h"

#include "GrowOnlySpanAllocator.h"

class FRHICommandList;
class FScene;
class FViewInfo;

extern void UploadDynamicPrimitiveShaderDataForView(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View);
extern void UpdateGPUScene(FRHICommandListImmediate& RHICmdList, FScene& Scene);
extern RENDERER_API void AddPrimitiveToUpdateGPU(FScene& Scene, int32 PrimitiveId);

class FGPUScene
{
public:
	FGPUScene()
		: bUpdateAllPrimitives(false)
		, InstanceDataSOAStride(0)
	{
	}

	/**
	 * Allocates a range of space in the instance data buffer for the required number of instances, 
	 * returns the offset to the first instance or INDEX_NONE if either the allocation failed or NumInstanceDataEntries was zero.
	 * Marks the instances as requiring update (actual update is handled later).
	 */
	int32 AllocateInstanceSlots(int32 NumInstanceDataEntries);
	
	/**
	 * Free the instance data slots for reuse.
	 */
	void FreeInstanceSlots(int32 InstanceDataOffset, int32 NumInstanceDataEntries);

	/**
	 * Flag the primitive as added this frame (flags are consumed / reset when the GPU-Scene update is invoked).
	 */
	void MarkPrimitiveAdded(int32 PrimitiveId);

	bool bUpdateAllPrimitives;

	/** Indices of primitives that need to be updated in GPU Scene */
	TArray<int32>			PrimitivesToUpdate;

	/** Bit array of all scene primitives. Set bit means that current primitive is in PrimitivesToUpdate array. */
	TBitArray<>				PrimitivesMarkedToUpdate;

	/** GPU mirror of Primitives */
	/** Only one of the resources(TextureBuffer or Texture2D) will be used depending on the Mobile.UseGPUSceneTexture cvar */
	FRWBufferStructured PrimitiveBuffer;
	FTextureRWBuffer2D PrimitiveTexture;
	FScatterUploadBuffer PrimitiveUploadBuffer;
	FScatterUploadBuffer PrimitiveUploadViewBuffer;

	/** GPU primitive instance list */
	TBitArray<>				InstanceDataToClear;
	FGrowOnlySpanAllocator	InstanceDataAllocator;
	FRWBufferStructured		InstanceDataBuffer;
	FScatterUploadBuffer	InstanceUploadBuffer;
	uint32					InstanceDataSOAStride;	// Distance between arrays in float4s
	TSet<uint32>			InstanceClearList;

	/** GPU light map data */
	FGrowOnlySpanAllocator	LightmapDataAllocator;
	FRWBufferStructured		LightmapDataBuffer;
	FScatterUploadBuffer	LightmapUploadBuffer;

	/** Flag array with 1 bit set for each newly added primitive. */
	TBitArray<>				AddedPrimitiveFlags;
	
	/** 
	 * Stores a copy of the Scene.GetFrameNumber() when updated. Used to track which primitives/instances are updated.
	 * When using GPU-Scene for rendering it should ought to be the same as that stored in the Scene (otherwise they are not in sync).
	 */
	uint32 SceneFrameNumber;
};
