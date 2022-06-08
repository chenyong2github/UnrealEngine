// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementCone.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"

void UGizmoElementCone::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	if (!IsVisible())
	{
		return;
	}

	check(RenderAPI);
	const FSceneView* View = RenderAPI->GetSceneView();

	FTransform LocalToWorldTransform = RenderState.LocalToWorldTransform;

	bool bVisibleViewDependent = GetViewDependentVisibility(View, LocalToWorldTransform, Origin);

	if (bVisibleViewDependent)
	{
		const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState);

		if (UseMaterial)
		{
			FQuat AlignRot;
			const bool bHasAlignRot = GetViewAlignRot(View, LocalToWorldTransform, Origin, AlignRot);
			const FVector AdjustedDir = bHasAlignRot ? AlignRot.RotateVector(Direction) : Direction;

			FQuat Rotation = FRotationMatrix::MakeFromX(AdjustedDir).ToQuat();

			const FVector Scale(Height);

			FTransform RenderLocalToWorldTransform = FTransform(Rotation, Origin, Scale) * LocalToWorldTransform;
			const float ConeSide = FMath::Sqrt(Height * Height + Radius * Radius);
			const float Angle = FMath::Acos(Height / ConeSide);
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
			DrawCone(PDI, RenderLocalToWorldTransform.ToMatrixWithScale(), Angle, Angle, NumSides, false, FColor::White, UseMaterial->GetRenderProxy(), SDPG_Foreground);
		}
	}
	CacheRenderState(LocalToWorldTransform, RenderState.PixelToWorldScale, bVisibleViewDependent);
}



FInputRayHit UGizmoElementCone::LineTrace(const FVector RayOrigin, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		bool bIntersects = false;
		double RayParam = 0.0;

		const double PixelHitThresholdAdjust = CachedPixelToWorldScale * PixelHitDistanceThreshold;
		const double ConeSide = FMath::Sqrt(Height * Height + Radius * Radius);
		const double CosAngle = Height / ConeSide;
		const double ConeHeight = Height * CachedLocalToWorldTransform.GetScale3D().X + PixelHitThresholdAdjust * 2.0;
		const FVector ConeDirection = CachedLocalToWorldTransform.TransformVectorNoScale(Direction);
		const FVector ConeOrigin = CachedLocalToWorldTransform.TransformPosition(Origin) - ConeDirection * PixelHitThresholdAdjust;

		GizmoMath::RayConeIntersection(
			ConeOrigin,
			ConeDirection,
			CosAngle,
			ConeHeight,
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

FBoxSphereBounds UGizmoElementCone::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementCone::SetOrigin(const FVector& InOrigin)
{
	Origin = InOrigin;
}

FVector UGizmoElementCone::GetOrigin() const
{
	return Origin;
}

void UGizmoElementCone::SetDirection(const FVector& InDirection)
{
	Direction = InDirection;
	Direction.Normalize();
}

FVector UGizmoElementCone::GetDirection() const
{
	return Direction;
}

void UGizmoElementCone::SetHeight(float InHeight)
{
	Height = InHeight;
}

float UGizmoElementCone::GetHeight() const
{
	return Height;
}

void UGizmoElementCone::SetRadius(float InRadius)
{
	Radius = InRadius;
}

float UGizmoElementCone::GetRadius() const
{
	return Radius;
}

void UGizmoElementCone::SetNumSides(int32 InNumSides)
{
	NumSides = InNumSides;
}

int32 UGizmoElementCone::GetNumSides() const
{
	return NumSides;
}

