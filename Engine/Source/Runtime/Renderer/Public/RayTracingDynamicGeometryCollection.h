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

	void BeginUpdate();
	void DispatchUpdates(FRHIComputeCommandList& RHICmdList);
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
};

#endif // RHI_RAYTRACING
