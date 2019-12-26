// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RayTracingInstance.cpp: Helper functions for creating a ray tracing instance.
=============================================================================*/

#include "RayTracingInstance.h"

#if RHI_RAYTRACING

void FRayTracingInstance::BuildInstanceMaskAndFlags()
{
	ensureMsgf(Materials.Num() > 0, TEXT("You need to add materials first for instance mask and flags to build upon."));

	Mask = 0;

	bool bAllSegmentsOpaque = true;
	bool bAnySegmentsCastShadow = false;
	bool bAllSegmentsCastShadow = true;

	for (int32 SegmentIndex = 0; SegmentIndex < Materials.Num(); SegmentIndex++)
	{
		FMeshBatch& MeshBatch = Materials[SegmentIndex];

		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(ERHIFeatureLevel::SM5, FallbackMaterialRenderProxyPtr);
		const EBlendMode BlendMode = Material.GetBlendMode();
		Mask |= ComputeBlendModeMask(BlendMode);
		bAllSegmentsOpaque &= BlendMode == BLEND_Opaque;
		bAnySegmentsCastShadow |= MeshBatch.CastRayTracedShadow && Material.CastsRayTracedShadows();
		bAllSegmentsCastShadow &= MeshBatch.CastRayTracedShadow && Material.CastsRayTracedShadows();
	}

	bForceOpaque = bAllSegmentsOpaque && bAllSegmentsCastShadow;
	Mask |= bAnySegmentsCastShadow ? RAY_TRACING_MASK_SHADOW : 0;
}

uint8 ComputeBlendModeMask(const EBlendMode BlendMode)
{
	return (BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked) ? RAY_TRACING_MASK_OPAQUE : RAY_TRACING_MASK_TRANSLUCENT;
}

#endif
