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
		const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState, ElementInteractionState);

		if (UseMaterial)
		{
			FQuat AlignRot;
			const bool bHasAlignRot = GetViewAlignRot(View, LocalToWorldTransform, Base, AlignRot);
			const FVector AdjustedDir = bHasAlignRot ? AlignRot.RotateVector(Direction) : Direction;

			const FQuat Rotation = FRotationMatrix::MakeFromZ(AdjustedDir).ToQuat();
			const float HalfHeight = Height * 0.5f;
			const FVector Origin = Base + AdjustedDir * HalfHeight;

			LocalToWorldTransform = FTransform(Rotation, Origin) * LocalToWorldTransform;
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
			DrawCylinder(PDI, LocalToWorldTransform.ToMatrixWithScale(), FVector::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Radius, HalfHeight, NumSides, UseMaterial->GetRenderProxy(), SDPG_Foreground);

		}
	}
	CacheRenderState(LocalToWorldTransform, bVisibleViewDependent);
}



FInputRayHit UGizmoElementCylinder::LineTrace(const FVector RayOrigin, const FVector RayDirectionm)
{
	if (IsHittableInView())
	{
		// @todo - modify ray-cylinder intersection to work with updated properties
#if 0
		bool bIntersects = false;
		double RayParam = 0.0;

		const FTransform LocalToWorldTransform = FTransform(Base) * InLocalToWorldTransform;
		const double WorldConeHeight = Height * InLocalToWorldTransform.GetScale3D().X;
		const FVector ConeOrigin = Base + Height * Direction;
		const FVector WorldConeOrigin = LocalToWorldTransform.TransformPosition(ConeOrigin);
		const FVector WorldConeDirection = LocalToWorldTransform.TransformVector(Direction);
		const double ConeSide = FMath::Sqrt(Height * Height + Radius * Radius);
		const double CosAngle = Radius / ConeSide;

		GizmoMath::RayConeIntersection(
			WorldConeOrigin,
			WorldConeDirection,
			CosAngle,
			WorldConeHeight,
			RayOrigin, RayDirection,
			bIntersects, RayParam);

		if (bIntersects)
		{
			FInputRayHit Hit;
			Hit.HitObject = this;
			return Hit;
		}
#endif
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
