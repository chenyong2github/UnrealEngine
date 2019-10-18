// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if RHI_RAYTRACING

class RENDERER_API FRayTracingDynamicGeometryCollection
{
public:
	FRayTracingDynamicGeometryCollection();

	void AddDynamicMeshBatchForGeometryUpdate(
		const FScene* Scene, 
		const FSceneView* View, 
		const FPrimitiveSceneProxy* PrimitiveSceneProxy, 
		FRayTracingDynamicGeometryUpdateParams Params,
		uint32 PrimitiveId 
	);

	template<typename CmdListType>
	void DispatchUpdates(CmdListType& RHICmdList);

	void Clear();

private:
	TUniquePtr<TArray<struct FMeshComputeDispatchCommand>> DispatchCommands;
	TArray<FAccelerationStructureBuildParams> BuildParams;
};

#endif // RHI_RAYTRACING
