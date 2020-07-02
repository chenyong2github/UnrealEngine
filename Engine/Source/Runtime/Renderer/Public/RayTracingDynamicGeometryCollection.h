// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if RHI_RAYTRACING

class RENDERER_API FRayTracingDynamicGeometryCollection
{
public:
	FRayTracingDynamicGeometryCollection();
	~FRayTracingDynamicGeometryCollection();

	void AddDynamicMeshBatchForGeometryUpdate(
		const FScene* Scene, 
		const FSceneView* View, 
		const FPrimitiveSceneProxy* PrimitiveSceneProxy, 
		FRayTracingDynamicGeometryUpdateParams Params,
		uint32 PrimitiveId 
	);
	uint64 GetSharedBufferGenerationID() const { return SharedBufferGenerationID; }

	void BeginUpdate();
	void DispatchUpdates(FRHIComputeCommandList& ParentCmdList);
	void EndUpdate(FRHICommandListImmediate& RHICmdList);

private:
	TArray<struct FMeshComputeDispatchCommand> DispatchCommands;
	TArray<FAccelerationStructureBuildParams> BuildParams;
	TArray<FRayTracingGeometrySegment> Segments;

	struct FVertexPositionBuffer
	{
		FRWBuffer RWBuffer;
		uint32 UsedSize = 0;
	};
	TArray<FVertexPositionBuffer*> VertexPositionBuffers;

	// Generation ID when the shared vertex buffers have been reset. The current generation ID is stored in the FRayTracingGeometry to keep track
	// if the vertex buffer data is still valid for that frame - validated before generation the TLAS
	uint64 SharedBufferGenerationID = 0;
};

#endif // RHI_RAYTRACING
