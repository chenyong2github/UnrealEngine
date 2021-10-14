// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"
#include "RenderResource.h"

class FViewInfo;
class FSortedIndexBuffer;
struct FMeshBatchElementDynamicIndexBuffer;
class FViewInfo;

struct FSortedTriangleData
{
	const FIndexBuffer* SourceIndexBuffer = nullptr;
	FIndexBuffer* SortedIndexBuffer = nullptr;

	FShaderResourceViewRHIRef  SourceIndexSRV = nullptr;
	FUnorderedAccessViewRHIRef SortedIndexUAV = nullptr;

	uint32 SortedFirstIndex = 0;
	uint32 SourceFirstIndex	= 0;
	uint32 NumPrimitives = 0;
	uint32 NumIndices = 0;

	EPrimitiveType SourcePrimitiveType = PT_TriangleList;
	EPrimitiveType SortedPrimitiveType = PT_TriangleList;

	bool IsValid() const { return SortedIndexBuffer != nullptr; }
};

struct FOITSceneData
{
	/* Allocate sorted-triangle data for a instance */
	FSortedTriangleData Allocate(const FIndexBuffer* InSource, EPrimitiveType PrimitiveType, uint32 InFirstIndex, uint32 InNumPrimitives);

	/* Deallocate sorted-triangle data */
	void Deallocate(FIndexBuffer* IndexBuffer);

	TArray<FSortedTriangleData> Allocations;
	TArray<FSortedIndexBuffer*> FreeBuffers;
	TQueue<uint32> FreeSlots;
	uint32 FrameIndex = 0;
};

enum class FOITSortingType
{
	FrontToBack,
	BackToFront,
};

namespace OIT
{
	/* Return true if OIT techniques are enabled/supported */
	bool IsEnabled(const FViewInfo& View);
	bool IsEnabled(EShaderPlatform ShaderPlatform);

	/* Return true if the current MeshBatch is compatible with per-instance sorted triangle */
	bool IsCompatible(const FMeshBatch& Mesh, ERHIFeatureLevel::Type InFeatureLevel);

	/* Sort triangles of all instances whose has the sorted triangle option enabled */
	void AddSortTrianglesPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FOITSceneData& OITSceneData, FOITSortingType SortType);

	/* Convert FSortedTriangleData into FMeshBatchElementDynamicIndexBuffer */
	void ConvertSortedIndexToDynamicIndex(FSortedTriangleData* In, FMeshBatchElementDynamicIndexBuffer* Out);
}
 
