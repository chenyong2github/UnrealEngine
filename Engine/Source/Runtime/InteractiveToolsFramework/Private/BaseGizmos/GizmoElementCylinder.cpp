// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementCylinder.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"

void UGizmoElementCylinder::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	if (!IsVisible())
	{
		return;
	}

	check(RenderAPI);
	const FSceneView* View = RenderAPI->GetSceneView();

	FTransform LocalToWorldTransform = RenderState.LocalToWorldTransform;

	bool bVisibleViewDependent = GetViewDependentVisibility(View, LocalToWorldTransform, Base);

	if (bVisibleViewDependent)
	{
		const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState);

		if (UseMaterial)
		{
			FQuat AlignRot;
			const bool bHasAlignRot = GetViewAlignRot(View, LocalToWorldTransform, Base, AlignRot);
			const FVector AdjustedDir = bHasAlignRot ? AlignRot.RotateVector(Direction) : Direction;

			const FQuat Rotation = FRotationMatrix::MakeFromZ(AdjustedDir).ToQuat();
			const double HalfHeight = Height * 0.5;
			const FVector Origin = Base + AdjustedDir * HalfHeight;

			FTransform RenderLocalToWorldTransform = FTransform(Rotation, Origin) * LocalToWorldTransform;
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
			DrawCylinder(PDI, RenderLocalToWorldTransform.ToMatrixWithScale(), FVector::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Radius, HalfHeight, NumSides, UseMaterial->GetRenderProxy(), SDPG_Foreground);

		}
	}
	CacheRenderState(LocalToWorldTransform, RenderState.PixelToWorldScale, bVisibleViewDependent);
}



FInputRayHit UGizmoElementCylinder::LineTrace(const FVector RayOrigin, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		bool bIntersects = false;
		double RayParam = 0.0;
		
		const double PixelHitThresholdAdjust = CachedPixelToWorldScale * PixelHitDistanceThreshold;
		const double CylinderHeight = Height * CachedLocalToWorldTransform.GetScale3D().X + PixelHitThresholdAdjust * 2.0;
		const double CylinderRadius = Radius * CachedLocalToWorldTransform.GetScale3D().X + PixelHitThresholdAdjust;
		const FVector CylinderDirection = CachedLocalToWorldTransform.TransformVectorNoScale(Direction);
		const FVector CylinderLocalCenter = Base + Direction * Height * 0.5;
		const FVector CylinderCenter = CachedLocalToWorldTransform.TransformPosition(CylinderLocalCenter);

		GizmoMath::RayCylinderIntersection(
			CylinderCenter,
			CylinderDirection,
			CylinderRadius,
			CylinderHeight,
			RayOrigin, RayDirection,
			bIntersects, RayParam);

		if (bIntersects)
		{
			FInputRayHit RayHit(RayParam);
			RayHit.SetHitObject(this);
			RayHit.HitIdentifier = PartIdentifier;
			return RayHit;
		}
	}

	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementCylinder::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementCylinder::SetBase(const FVector& InBase)
{
	Base = InBase;
}

FVector UGizmoElementCylinder::GetBase() const
{
	return Base;
}

void UGizmoElementCylinder::SetDirection(const FVector& InDirection)
{
	Direction = InDirection;
	Direction.Normalize();
}

FVector UGizmoElementCylinder::GetDirection() const
{
	return Direction;
}

void UGizmoElementCylinder::SetHeight(float InHeight)
{
	Height = InHeight;
}

float UGizmoElementCylinder::GetHeight() const
{
	return Height;
}

void UGizmoElementCylinder::SetRadius(float InRadius)
{
	Radius = InRadius;
}

float UGizmoElementCylinder::GetRadius() const
{
	return Radius;
}

void UGizmoElementCylinder::SetNumSides(int32 InNumSides)
{
	NumSides = InNumSides;
}

int32 UGizmoElementCylinder::GetNumSides() const
{
	return NumSides;
}
