// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementBox.h"
#include "BaseGizmos/GizmoMath.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "Materials/MaterialInterface.h"
#include "InputState.h"
#include "SceneManagement.h"

void UGizmoElementBox::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	if (!IsVisible())
	{
		return;
	}

	check(RenderAPI);
	const FSceneView* View = RenderAPI->GetSceneView();

	FTransform LocalToWorldTransform = RenderState.LocalToWorldTransform;

	bool bVisibleViewDependent = GetViewDependentVisibility(View, LocalToWorldTransform, Center);

	if (bVisibleViewDependent)
	{
		const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState, ElementInteractionState);

		if (UseMaterial)
		{
			FQuat AlignRot;
			FVector AdjustedSideDir, AdjustedUpDir;
			if (GetViewAlignRot(View, LocalToWorldTransform, Center, AlignRot))
			{
				AdjustedSideDir = AlignRot.RotateVector(SideDirection);
				AdjustedUpDir = AlignRot.RotateVector(UpDirection);
			}
			else
			{
				AdjustedSideDir = SideDirection;
				AdjustedUpDir = UpDirection;
			}

			FQuat Rotation = FRotationMatrix::MakeFromYZ(AdjustedSideDir, AdjustedUpDir).ToQuat();
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
			LocalToWorldTransform = FTransform(Rotation, Center) * LocalToWorldTransform;
			const FVector HalfDimensions = Dimensions * 0.5;
			DrawBox(PDI, LocalToWorldTransform.ToMatrixWithScale(), HalfDimensions, UseMaterial->GetRenderProxy(), SDPG_Foreground);
		}
	}

	CacheRenderState(LocalToWorldTransform, bVisibleViewDependent);
}



FInputRayHit UGizmoElementBox::LineTrace(const FVector RayCenter, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		// @todo - modify ray-box intersection to work with updated properties

#if 0
		bool bIntersects = false;
		float RayParam = 0.0f;


		const FVector BoxOrigin = Origin + Offset;
		GizmoMath::RayBoxIntersection(
			BoxOrigin, CubeObject->Axis, CubeObject->Radius, CubeObject->Height,
			ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction,
			bIntersects, RayParam);

		if (bIntersects)
		{
			return FInputRayHit(RayParam);
		}
#endif
	}

	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementBox::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementBox::SetCenter(const FVector& InCenter)
{
	Center = InCenter;
}

FVector UGizmoElementBox::GetCenter() const
{
	return Center;
}

void UGizmoElementBox::SetUpDirection(const FVector& InUpDirection)
{
	UpDirection = InUpDirection;
	UpDirection.Normalize();
}

FVector UGizmoElementBox::GetUpDirection() const
{
	return UpDirection;
}

void UGizmoElementBox::SetSideDirection(const FVector& InSideDirection)
{
	SideDirection = InSideDirection;
	SideDirection.Normalize();
}

FVector UGizmoElementBox::GetSideDirection() const
{
	return SideDirection;
}

FVector UGizmoElementBox::GetDimensions() const
{
	return Dimensions;
}

void UGizmoElementBox::SetDimensions(const FVector& InDimensions)
{
	Dimensions = InDimensions;
}

