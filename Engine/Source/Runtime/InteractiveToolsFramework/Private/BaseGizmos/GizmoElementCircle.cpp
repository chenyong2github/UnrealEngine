// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementCircle.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Intersection/IntersectionUtil.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"

void UGizmoElementCircle::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
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
			FVector AdjustedNormal;
			FQuat AlignRot;
			if (GetViewAlignRot(View, LocalToWorldTransform, Center, AlignRot))
			{
				AdjustedNormal = AlignRot.RotateVector(Normal);
			}
			else
			{
				AdjustedNormal = Normal;
			}

			const FVector WorldNormal = LocalToWorldTransform.TransformVectorNoScale(AdjustedNormal);
			GizmoMath::MakeNormalPlaneBasis(WorldNormal, Axis0, Axis1);
			Axis0.Normalize();
			Axis1.Normalize();
		}

		const float WorldRadius = Radius * LocalToWorldTransform.GetScale3D().X;
		const FVector WorldCenter = LocalToWorldTransform.TransformPosition(Center);

		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

		if (bDrawMesh)
		{
			if (const UMaterialInterface* UseMaterial = GetCurrentMaterial(RenderState))
			{
				DrawDisc(PDI, WorldCenter, Axis0, Axis1, VertexColor, WorldRadius, NumSides, UseMaterial->GetRenderProxy(), SDPG_Foreground);
			}
		}

		if (bDrawLine)
		{
			DrawCircle(PDI, WorldCenter, Axis0, Axis1, LineColor, WorldRadius, NumSides, SDPG_Foreground, GetCurrentLineThickness());
		}
	}
	CacheRenderState(LocalToWorldTransform, RenderState.PixelToWorldScale, bVisibleViewDependent);
}

FInputRayHit UGizmoElementCircle::LineTrace(const FVector RayOrigin, const FVector RayDirection)
{
	if (IsHittableInView())
	{
		if (bHitMesh)
		{
			bool bIntersects = false;
			double Param = 0.0;

			const FVector WorldNormal = CachedLocalToWorldTransform.TransformVectorNoScale(Normal);
			const FVector WorldCenter = CachedLocalToWorldTransform.TransformPosition(Center);
			const double PixelHitThresholdAdjust = CachedPixelToWorldScale * PixelHitDistanceThreshold;
			const double WorldRadius = CachedLocalToWorldTransform.GetScale3D().X * Radius + PixelHitThresholdAdjust;

			UE::Geometry::FLinearIntersection Result;
			IntersectionUtil::RayCircleIntersection(RayOrigin, RayDirection, WorldCenter, WorldRadius, WorldNormal, Result);
			
			if (Result.intersects)
			{
				FInputRayHit RayHit(Result.parameter.Min);
				RayHit.SetHitObject(this);
				RayHit.HitIdentifier = PartIdentifier;
				return RayHit;
			}
		}
		else if (bHitLine)
		{
			// @todo - add hit testing for line-drawn circle, requires storing PixelToWorld scale factor
		}
	}
	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementCircle::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementCircle::SetCenter(FVector InCenter)
{
	Center = InCenter;
}

FVector UGizmoElementCircle::GetCenter() const
{
	return Center;
}

void UGizmoElementCircle::SetNormal(FVector InNormal)
{
	Normal = InNormal;
	Normal.Normalize();
}

FVector UGizmoElementCircle::GetNormal() const
{
	return Normal;
}

void UGizmoElementCircle::SetRadius(float InRadius)
{
	Radius = InRadius;
}

float UGizmoElementCircle::GetRadius() const
{
	return Radius;
}

void UGizmoElementCircle::SetNumSides(int InNumSides)
{
	NumSides = InNumSides;
}

int UGizmoElementCircle::GetNumSides() const
{
	return NumSides;
}

void UGizmoElementCircle::SetLineColor(const FLinearColor& InColor)
{
	LineColor = InColor;
}

FLinearColor UGizmoElementCircle::GetLineColor() const
{
	return LineColor;
}

void UGizmoElementCircle::SetDrawMesh(bool InDrawMesh)
{
	bDrawMesh = InDrawMesh;
}

bool UGizmoElementCircle::GetDrawMesh() const
{
	return bDrawMesh;
}

void UGizmoElementCircle::SetDrawLine(bool InDrawLine)
{
	bDrawLine = InDrawLine;
}

bool UGizmoElementCircle::GetDrawLine() const
{
	return bDrawLine;
}

void UGizmoElementCircle::SetHitMesh(bool InHitMesh)
{
	bHitMesh = InHitMesh;
}

bool UGizmoElementCircle::GetHitMesh() const
{
	return bHitMesh;
}

void UGizmoElementCircle::SetHitLine(bool InHitLine)
{
	bHitLine = InHitLine;
}

bool UGizmoElementCircle::GetHitLine() const
{
	return bHitLine;
}

void UGizmoElementCircle::SetScreenSpace(bool InScreenSpace)
{
	bScreenSpace = InScreenSpace;
}

bool UGizmoElementCircle::GetScreenSpace()
{
	return bScreenSpace;
}
