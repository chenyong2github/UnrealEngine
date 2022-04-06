// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementRectangle.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"

void UGizmoElementRectangle::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
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
		FVector Axis0, Axis1;
		if (bScreenSpace)
		{
			Axis0 = View->GetViewUp();
			Axis1 = View->GetViewRight();
		}
		else
		{
			FQuat AlignRot;
			if (GetViewAlignRot(View, LocalToWorldTransform, Center, AlignRot))
			{
				Axis0 = AlignRot.RotateVector(UpDirection);
				Axis1 = AlignRot.RotateVector(SideDirection);
			}
			else
			{
				Axis0 = UpDirection;
				Axis1 = SideDirection;
			}

			Axis0 = LocalToWorldTransform.TransformVectorNoScale(Axis0);
			Axis1 = LocalToWorldTransform.TransformVectorNoScale(Axis1);
		}

		FVector WorldCenter = LocalToWorldTransform.TransformPosition(Center);
		float WorldWidth = Width * LocalToWorldTransform.GetScale3D().X;
		float WorldHeight = Height * LocalToWorldTransform.GetScale3D().X;

		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

		if (bDrawMesh)
		{
			if (const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState, ElementInteractionState))
			{

				DrawRectangleMesh(PDI, WorldCenter, Axis0, Axis1, VertexColor, WorldWidth, WorldHeight, UseMaterial->GetRenderProxy(), SDPG_Foreground);
			}
		}
		if (bDrawLine)
		{
			DrawRectangle(PDI, WorldCenter, Axis0, Axis1, LineColor, WorldWidth, WorldHeight, SDPG_Foreground, 0.0f);
		}
	}
	CacheRenderState(LocalToWorldTransform, bVisibleViewDependent);
}

FInputRayHit UGizmoElementRectangle::LineTrace(const FVector RayOrigin, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		// @todo - implement ray-circle intersection
	}
	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementRectangle::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementRectangle::SetCenter(FVector InCenter)
{
	Center = InCenter;
}

FVector UGizmoElementRectangle::GetCenter() const
{
	return Center;
}

void UGizmoElementRectangle::SetWidth(float InWidth)
{
	Width = InWidth;
	
}

float UGizmoElementRectangle::GetWidth() const
{
	return Width;
}

void UGizmoElementRectangle::SetHeight(float InHeight)
{
	Height = InHeight;
}

float UGizmoElementRectangle::GetHeight() const
{
	return Height;
}

void UGizmoElementRectangle::SetUpDirection(const FVector& InUpDirection)
{
	UpDirection = InUpDirection.GetSafeNormal();
}

FVector UGizmoElementRectangle::GetUpDirection() const
{
	return UpDirection;
}

void UGizmoElementRectangle::SetSideDirection(const FVector& InSideDirection)
{
	SideDirection = InSideDirection.GetSafeNormal();
}

FVector UGizmoElementRectangle::GetSideDirection() const
{
	return SideDirection;
}

void UGizmoElementRectangle::SetScreenSpace(bool InScreenSpace)
{
	bScreenSpace = InScreenSpace;
}

bool UGizmoElementRectangle::GetScreenSpace()
{
	return bScreenSpace;
}

void UGizmoElementRectangle::SetLineColor(const FColor& InLineColor)
{
	LineColor = InLineColor;
}

FColor UGizmoElementRectangle::GetLineColor()
{
	return LineColor;
}

void UGizmoElementRectangle::SetDrawMesh(bool InDrawMesh)
{
	bDrawMesh = InDrawMesh;
}
bool UGizmoElementRectangle::GetDrawMesh() const
{
	return bDrawMesh;
}

void UGizmoElementRectangle::SetDrawLine(bool InDrawLine)
{
	bDrawLine = InDrawLine;
}

bool UGizmoElementRectangle::GetDrawLine() const
{
	return bDrawLine;
}

void UGizmoElementRectangle::SetHitMesh(bool InHitMesh)
{
	bHitMesh = InHitMesh;
}

bool UGizmoElementRectangle::GetHitMesh() const
{
	return bHitMesh;
}

void UGizmoElementRectangle::SetHitLine(bool InHitLine)
{
	bHitLine = InHitLine;
}

bool UGizmoElementRectangle::GetHitLine() const
{
	return bHitLine;
}
