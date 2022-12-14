// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RayTracingInstance.cpp: Helper functions for creating a ray tracing instance.
=============================================================================*/

#include "RayTracingInstance.h"

#if RHI_RAYTRACING

#include "RayTracingDefinitions.h"
#include "MaterialShared.h"
#include "RayTracing/RayTracingInstanceMask.h"

void FRayTracingInstance::BuildInstanceMaskAndFlags(ERHIFeatureLevel::Type FeatureLevel)
{
	TArrayView<const FMeshBatch> MeshBatches = GetMaterials();

	// add extra mask bit for hair.
	uint8 ExtraMask = bThinGeometry ? RAY_TRACING_MASK_HAIR_STRANDS : 0;

	FRayTracingMaskAndFlags MaskAndFlags = BuildRayTracingInstanceMaskAndFlags(MeshBatches, FeatureLevel, InstanceLayer, ExtraMask);

	Mask = MaskAndFlags.Mask;
	bForceOpaque = bForceOpaque || MaskAndFlags.bForceOpaque;
	bDoubleSided = bDoubleSided || MaskAndFlags.bDoubleSided;
}

FRayTracingMaskAndFlags BuildRayTracingInstanceMaskAndFlags(TArrayView<const FMeshBatch> MeshBatches, ERHIFeatureLevel::Type FeatureLevel, ERayTracingInstanceLayer InstanceLayer, uint8 ExtraMask)
{
	FRayTracingMaskAndFlags Result;

	ensureMsgf(MeshBatches.Num() > 0, TEXT("You need to add MeshBatches first for instance mask and flags to build upon."));

	Result.Mask = ExtraMask;

	bool bAllSegmentsOpaque = true;
	bool bAnySegmentsCastShadow = false;
	bool bAllSegmentsCastShadow = true;
	bool bDoubleSided = false;

	for (int32 SegmentIndex = 0; SegmentIndex < MeshBatches.Num(); SegmentIndex++)
	{
		const FMeshBatch& MeshBatch = MeshBatches[SegmentIndex];

		// Mesh Batches can "null" when they have zero triangles.  Check the MaterialRenderProxy before accessing.
		if (MeshBatch.bUseForMaterial && MeshBatch.MaterialRenderProxy)
		{
			const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetIncompleteMaterialWithFallback(FeatureLevel);
			const EBlendMode BlendMode = Material.GetBlendMode();
			Result.Mask |= BlendModeToRayTracingInstanceMask(BlendMode);
			bAllSegmentsOpaque &= BlendMode == BLEND_Opaque;
			bAnySegmentsCastShadow |= MeshBatch.CastRayTracedShadow && Material.CastsRayTracedShadows();
			bAllSegmentsCastShadow &= MeshBatch.CastRayTracedShadow && Material.CastsRayTracedShadows();
			bDoubleSided |= MeshBatch.bDisableBackfaceCulling || Material.IsTwoSided();
		}
	}

	Result.bForceOpaque = bAllSegmentsOpaque && bAllSegmentsCastShadow;
	Result.bDoubleSided = bDoubleSided;

	Result.Mask |= bAnySegmentsCastShadow ? RAY_TRACING_MASK_SHADOW : 0;

	if (Result.Mask & RAY_TRACING_MASK_HAIR_STRANDS)
	{
		// For hair strands, opaque/translucent mask should be cleared to make sure geometry is only in the hair group. 
		// If any segment receives shadow, it should receive only thin shadow instead of shadow.

		Result.Mask &= ~(RAY_TRACING_MASK_SHADOW | RAY_TRACING_MASK_THIN_SHADOW | RAY_TRACING_MASK_TRANSLUCENT | RAY_TRACING_MASK_OPAQUE);

		if (bAnySegmentsCastShadow)
		{
			Result.Mask |= RAY_TRACING_MASK_THIN_SHADOW;
		}
	}

	if (InstanceLayer == ERayTracingInstanceLayer::FarField)
	{
		// if far field, set that flag exclusively
		Result.Mask = RAY_TRACING_MASK_FAR_FIELD;
	}

	return Result;
}

uint8 ComputeBlendModeMask(const EBlendMode BlendMode)
{
	return BlendModeToRayTracingInstanceMask(BlendMode);
}

#endif
