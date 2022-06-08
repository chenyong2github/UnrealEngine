// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementRectangle.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"
#include "Math/UnrealMathUtility.h"

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
			if (const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState))
			{

				DrawRectangleMesh(PDI, WorldCenter, Axis0, Axis1, VertexColor, WorldWidth, WorldHeight, UseMaterial->GetRenderProxy(), SDPG_Foreground);
			}
		}
		if (bDrawLine)
		{
			DrawRectangle(PDI, WorldCenter, Axis0, Axis1, LineColor, WorldWidth, WorldHeight, SDPG_Foreground, GetCurrentLineThickness());
		}
	}
	CacheRenderState(LocalToWorldTransform, RenderState.PixelToWorldScale, bVisibleViewDependent);
}

FInputRayHit UGizmoElementRectangle::LineTrace(const FVector RayOrigin, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		if (bHitMesh)
		{
			const FVector UpAxis = CachedLocalToWorldTransform.TransformVectorNoScale(UpDirection);
			const FVector SideAxis = CachedLocalToWorldTransform.TransformVectorNoScale(SideDirection);
			const FVector Normal = FVector::CrossProduct(UpAxis, SideAxis);
			const FVector WorldCenter = CachedLocalToWorldTransform.TransformPosition(Center);
			const double Scale = CachedLocalToWorldTransform.GetScale3D().X;
			const double PixelHitThresholdAdjust = CachedPixelToWorldScale * PixelHitDistanceThreshold;
			const double WorldHeight = Scale * Height + 2.0 * PixelHitThresholdAdjust;
			const double WorldWidth = Scale * Width + 2.0 * PixelHitThresholdAdjust;
			const FVector Base = WorldCenter - UpAxis * WorldHeight * 0.5 - SideAxis * WorldWidth * 0.5;

			// if ray is parallel to rectangle, no hit
			if (FMath::IsNearlyZero(FVector::DotProduct(Normal, RayDirection)))
			{
				return FInputRayHit();
			}

			FPlane Plane(Base, Normal);
			double HitDepth = FMath::RayPlaneIntersectionParam(RayOrigin, RayDirection, Plane);
			if (HitDepth < 0)
			{
				return FInputRayHit();
			}

			FVector HitPoint = RayOrigin + RayDirection * HitDepth;
			FVector HitOffset = HitPoint - Base;
			double HdU = FVector::DotProduct(HitOffset, UpAxis);
			double HdS = FVector::DotProduct(HitOffset, SideAxis);

			// clip to rectangle dimensions
			if (HdU >= 0.0 && HdU <= WorldHeight && HdS >= 0.0 && HdS <= WorldWidth)
			{
				FInputRayHit RayHit(HitDepth);
				RayHit.SetHitObject(this);
				RayHit.HitIdentifier = PartIdentifier;
				return RayHit;
			}
		}
		else if (bHitLine)
		{
			// @todo - add hit testing for line-drawn rectangle, requires storing PixelToWorld scale factor
		}
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

bool UGizmoElementRectangle::GetScreenSpace() const
{
	return bScreenSpace;
}

void UGizmoElementRectangle::SetLineColor(const FColor& InLineColor)
{
	LineColor = InLineColor;
}

FColor UGizmoElementRectangle::GetLineColor() const
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
