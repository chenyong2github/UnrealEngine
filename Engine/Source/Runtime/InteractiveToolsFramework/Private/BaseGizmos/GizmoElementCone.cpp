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

	bool bVisibleViewDependent = GetViewDependentVisibility(View, LocalToWorldTransform, Base);

	if (bVisibleViewDependent)
	{
		const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState, ElementInteractionState);

		if (UseMaterial)
		{
			FQuat AlignRot;
			const bool bHasAlignRot = GetViewAlignRot(View, LocalToWorldTransform, Base, AlignRot);
			const FVector AdjustedDir = bHasAlignRot ? AlignRot.RotateVector(Direction) : Direction;

			FQuat Rotation = FRotationMatrix::MakeFromX(-AdjustedDir).ToQuat();

			const FVector Scale(Height);
			const FVector Origin = Base + AdjustedDir * Height;

			LocalToWorldTransform = FTransform(Rotation, Origin, Scale) * LocalToWorldTransform;
			const float ConeSide = FMath::Sqrt(Height * Height + Radius * Radius);
			const float Angle = FMath::Acos(Height / ConeSide);
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
			DrawCone(PDI, LocalToWorldTransform.ToMatrixWithScale(), Angle, Angle, NumSides, false, FColor::White, UseMaterial->GetRenderProxy(), SDPG_Foreground);
		}
	}
	CacheRenderState(LocalToWorldTransform, bVisibleViewDependent);
}



FInputRayHit UGizmoElementCone::LineTrace(const FVector RayOrigin, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		// @todo - modify ray-cone intersection to work with updated properties

#if 0
		bool bIntersects = false;
		double RayParam = 0.0;

		const FMatrix LocalToWorldMatrix = LocalToWorldTransform.ToMatrixNoScale();

		const FVector Origin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);
		const FVector ConeDirection = (bWorld) ? Direction : FVector{ LocalToWorldMatrix.TransformVector(Direction) };
		const double ConeHeight = Height * DynamicPixelToWorldScale;
		const double ConeOffset = Offset * DynamicPixelToWorldScale;
		const FVector ConeOrigin = Origin + ConeOffset * ConeDirection;

		GizmoMath::RayConeIntersection(
			ConeOrigin, 
			ConeDirection, 
			FMath::Cos(Angle), 
			ConeHeight,
			RayOrigin, RayDirection,
			bIntersects, RayParam);

		if (bIntersects)
		{
			return FInputRayHit(RayParam);
		}
#endif
	}

	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementCone::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementCone::SetBase(const FVector& InBase)
{
	Base = InBase;
}

FVector UGizmoElementCone::GetBase() const
{
	return Base;
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

