// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingInstanceMask.h"

#if RHI_RAYTRACING

#include "RayTracingDefinitions.h"
#include "PrimitiveSceneProxy.h"

uint8 BlendModeToRayTracingInstanceMask(const EBlendMode BlendMode)
{
	return (BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked) ? RAY_TRACING_MASK_OPAQUE : RAY_TRACING_MASK_TRANSLUCENT;
}

FRayTracingMaskAndFlags BuildRayTracingInstanceMaskAndFlags(const FRayTracingInstance& Instance, const FPrimitiveSceneProxy& PrimitiveSceneProxy)
{
	const TArrayView<const FMeshBatch> MeshBatches = Instance.GetMaterials();

	// add extra mask bit for hair.
	uint8 ExtraMask = Instance.bThinGeometry ? RAY_TRACING_MASK_HAIR_STRANDS : 0;

	FRayTracingMaskAndFlags MaskAndFlags = BuildRayTracingInstanceMaskAndFlags(MeshBatches, PrimitiveSceneProxy.GetScene().GetFeatureLevel(), Instance.InstanceLayer, ExtraMask);

	MaskAndFlags.bForceOpaque = MaskAndFlags.bForceOpaque || Instance.bForceOpaque;
	MaskAndFlags.bDoubleSided = MaskAndFlags.bDoubleSided || Instance.bDoubleSided;

	return MaskAndFlags;
}

#endif