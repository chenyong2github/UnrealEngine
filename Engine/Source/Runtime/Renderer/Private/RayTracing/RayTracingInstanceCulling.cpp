// Copyright Epic Games, Inc.All Rights Reserved.

#include "RayTracingInstanceCulling.h"
#include "Lumen/Lumen.h"

#if RHI_RAYTRACING

static TAutoConsoleVariable<int32> CVarRayTracingCulling(
	TEXT("r.RayTracing.Culling"),
	0,
	TEXT("Enable culling in ray tracing for objects that are behind the camera\n")
	TEXT(" 0: Culling disabled (default)\n")
	TEXT(" 1: Culling by distance and solid angle enabled. Only cull objects behind camera.\n")
	TEXT(" 2: Culling by distance and solid angle enabled. Cull objects in front and behind camera.\n")
	TEXT(" 3: Culling by distance OR solid angle enabled. Cull objects in front and behind camera."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingCullingPerInstance(
	TEXT("r.RayTracing.Culling.PerInstance"),
	1,
	TEXT(""),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRayTracingCullingRadius(
	TEXT("r.RayTracing.Culling.Radius"),
	10000.0f,
	TEXT("Do camera culling for objects behind the camera outside of this radius in ray tracing effects (default = 10000 (100m))"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRayTracingCullingAngle(
	TEXT("r.RayTracing.Culling.Angle"),
	1.0f,
	TEXT("Do camera culling for objects behind the camera with a projected angle smaller than this threshold in ray tracing effects (default = 5 degrees )"),
	ECVF_RenderThreadSafe);

int32 GetRayTracingCulling()
{
	return CVarRayTracingCulling.GetValueOnRenderThread();
}

float GetRayTracingCullingRadius()
{
	return CVarRayTracingCullingRadius.GetValueOnRenderThread();
}

int32 GetRayTracingCullingPerInstance()
{
	return CVarRayTracingCullingPerInstance.GetValueOnRenderThread();
}

void FRayTracingCullingParameters::Init(FViewInfo& View)
{
	CullInRayTracing = CVarRayTracingCulling.GetValueOnRenderThread();
	CullingRadius = CVarRayTracingCullingRadius.GetValueOnRenderThread();
	FarFieldCullingRadius = Lumen::GetFarFieldMaxTraceDistance();
	CullAngleThreshold = CVarRayTracingCullingAngle.GetValueOnRenderThread();
	AngleThresholdRatio = FMath::Tan(FMath::Min(89.99f, CullAngleThreshold) * PI / 180.0f);
	ViewOrigin = View.ViewMatrices.GetViewOrigin();
	ViewDirection = View.GetViewDirection();
	bCullAllObjects = CullInRayTracing == 2 || CullInRayTracing == 3;
	bCullByRadiusOrDistance = CullInRayTracing == 3;
	bIsRayTracingFarField = Lumen::UseFarField();
}

namespace RayTracing
{

bool ShouldCullBounds(const FRayTracingCullingParameters& CullingParameters, FBoxSphereBounds ObjectBounds, bool bIsFarFieldPrimitive)
{
	if (CullingParameters.CullInRayTracing > 0)
	{
		const float ObjectRadius = ObjectBounds.SphereRadius;
		const FVector ObjectCenter = ObjectBounds.Origin + 0.5 * ObjectBounds.BoxExtent;
		const FVector CameraToObjectCenter = FVector(ObjectCenter - CullingParameters.ViewOrigin);

		const bool bConsiderCulling = CullingParameters.bCullAllObjects || FVector::DotProduct(CullingParameters.ViewDirection, CameraToObjectCenter) < -ObjectRadius;

		if (bConsiderCulling)
		{
			const float CameraToObjectCenterLength = CameraToObjectCenter.Size();

			if (bIsFarFieldPrimitive)
			{
				if (CameraToObjectCenterLength > (CullingParameters.FarFieldCullingRadius + ObjectRadius))
				{
					return true;
				}
			}
			else
			{
				const bool bIsFarEnoughToCull = CameraToObjectCenterLength > (CullingParameters.CullingRadius + ObjectRadius);

				// Cull by solid angle: check the radius of bounding sphere against angle threshold
				const bool bAngleIsSmallEnoughToCull = ObjectRadius / CameraToObjectCenterLength < CullingParameters.AngleThresholdRatio;

				if (CullingParameters.bCullByRadiusOrDistance && (bIsFarEnoughToCull || bAngleIsSmallEnoughToCull))
				{
					return true;
				}
				else if (bIsFarEnoughToCull && bAngleIsSmallEnoughToCull)
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool ShouldSkipPerInstanceCullingForPrimitive(const FRayTracingCullingParameters& CullingParameters, FBoxSphereBounds ObjectBounds, FBoxSphereBounds SmallestInstanceBounds, bool bIsFarFieldPrimitive)
{
	bool bSkipCulling = false;

	const float ObjectRadius = ObjectBounds.SphereRadius;
	const FVector ObjectCenter = ObjectBounds.Origin + 0.5 * ObjectBounds.BoxExtent;
	const FVector CameraToObjectCenter = FVector(ObjectCenter - CullingParameters.ViewOrigin);

	const FVector CameraToFurthestInstanceCenter = CameraToObjectCenter * (CameraToObjectCenter.Size() + ObjectRadius + SmallestInstanceBounds.SphereRadius) / CameraToObjectCenter.Size();

	const bool bConsiderCulling = CullingParameters.bCullAllObjects || FVector::DotProduct(CullingParameters.ViewDirection, CameraToObjectCenter) < -ObjectRadius;

	if (bConsiderCulling)
	{
		const float CameraToObjectCenterLength = CameraToObjectCenter.Size();

		if (bIsFarFieldPrimitive)
		{
			if (CameraToObjectCenterLength < (CullingParameters.FarFieldCullingRadius - ObjectRadius))
			{
				bSkipCulling = true;
			}
		}
		else
		{
			const bool bSkipDistanceCulling = CameraToObjectCenterLength < (CullingParameters.CullingRadius - ObjectRadius);

			// Cull by solid angle: check the radius of bounding sphere against angle threshold
			const bool bSkipAngleCulling = FMath::IsFinite(SmallestInstanceBounds.SphereRadius / CameraToFurthestInstanceCenter.Size()) && SmallestInstanceBounds.SphereRadius / CameraToFurthestInstanceCenter.Size() >= CullingParameters.AngleThresholdRatio;

			if (CullingParameters.bCullByRadiusOrDistance)
			{
				if (bSkipDistanceCulling && bSkipAngleCulling)
				{
					bSkipCulling = true;
				}
			}
			else if (bSkipDistanceCulling || bSkipAngleCulling)
			{
				bSkipCulling = true;
			}
		}
	}
	else
	{
		bSkipCulling = true;
	}

	return bSkipCulling;
}

}

void FRayTracingCullPrimitiveInstancesClosure::operator()() const
{
	FMemory::Memset(OutInstanceActivationMask.GetData(), 0xFF, OutInstanceActivationMask.Num() * 4);

	if (!RayTracing::ShouldSkipPerInstanceCullingForPrimitive(*CullingParameters, Scene->PrimitiveBounds[PrimitiveIndex].BoxSphereBounds, SceneInfo->CachedRayTracingInstanceWorldBounds[SceneInfo->SmallestRayTracingInstanceWorldBoundsIndex], bIsFarFieldPrimitive))
	{
		for (int32 InstanceIndex = 0; InstanceIndex < SceneInfo->CachedRayTracingInstanceWorldTransforms.Num(); InstanceIndex++)
		{
			if (RayTracing::ShouldCullBounds(*CullingParameters, SceneInfo->CachedRayTracingInstanceWorldBounds[InstanceIndex], bIsFarFieldPrimitive))
			{
				OutInstanceActivationMask[InstanceIndex / 32] &= ~(1 << (InstanceIndex % 32));
			}
		}
	}
}

#endif
