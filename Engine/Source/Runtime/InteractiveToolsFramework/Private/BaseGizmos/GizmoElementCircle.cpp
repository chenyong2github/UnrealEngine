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

	FRenderTraversalState CurrentRenderState(RenderState);
	bool bVisibleViewDependent = UpdateRenderState(RenderAPI, Center, CurrentRenderState);

	if (bVisibleViewDependent)
	{
		const float WorldRadius = Radius * CurrentRenderState.LocalToWorldTransform.GetScale3D().X;
		const FVector WorldCenter = CurrentRenderState.LocalToWorldTransform.TransformPosition(FVector::ZeroVector);
		FVector WorldUpAxis, WorldSideAxis;
		const FVector WorldNormal = CurrentRenderState.LocalToWorldTransform.TransformVectorNoScale(Normal);
		GizmoMath::MakeNormalPlaneBasis(WorldNormal, WorldUpAxis, WorldSideAxis);
		WorldUpAxis.Normalize();
		WorldSideAxis.Normalize();

		FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

		if (bDrawMesh)
		{
			if (const UMaterialInterface* UseMaterial = CurrentRenderState.GetCurrentMaterial())
			{
				FColor VertexColor = CurrentRenderState.GetVertexColor().ToFColor(false);
				DrawDisc(PDI, WorldCenter, WorldUpAxis, WorldSideAxis, VertexColor, WorldRadius, NumSides, UseMaterial->GetRenderProxy(), SDPG_Foreground);
			}
		}

		if (bDrawLine)
		{
			DrawCircle(PDI, WorldCenter, WorldUpAxis, WorldSideAxis, CurrentRenderState.GetCurrentLineColor(), WorldRadius, NumSides, SDPG_Foreground, GetCurrentLineThickness());
		}
	}
}

FInputRayHit UGizmoElementCircle::LineTrace(const UGizmoViewContext* ViewContext, const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection)
{
	if (!IsHittable())
	{
		return FInputRayHit();
	}

	FLineTraceTraversalState CurrentLineTraceState(LineTraceState);
	bool bHittableViewDependent = UpdateLineTraceState(ViewContext, Center, CurrentLineTraceState);

	if (bHittableViewDependent)
	{
		const FVector WorldNormal = CurrentLineTraceState.LocalToWorldTransform.TransformVectorNoScale(Normal);
		const FVector WorldCenter = CurrentLineTraceState.LocalToWorldTransform.TransformPosition(FVector::ZeroVector);
		const double PixelHitThresholdAdjust = CurrentLineTraceState.PixelToWorldScale * PixelHitDistanceThreshold;
		double WorldRadius = CurrentLineTraceState.LocalToWorldTransform.GetScale3D().X * Radius;

		// if ray is parallel to circle, no hit
		if (FMath::IsNearlyZero(FVector::DotProduct(WorldNormal, RayDirection)))
		{
			return FInputRayHit();
		}

		if (bHitMesh)
		{
			WorldRadius += PixelHitThresholdAdjust;

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
			FPlane Plane(WorldCenter, WorldNormal);
			double HitDepth = FMath::RayPlaneIntersectionParam(RayOrigin, RayDirection, Plane);
			if (HitDepth < 0)
			{
				return FInputRayHit();
			}

			FVector HitPoint = RayOrigin + RayDirection * HitDepth;

			FVector NearestCirclePos;
			GizmoMath::ClosetPointOnCircle(HitPoint, WorldCenter, WorldNormal, WorldRadius, NearestCirclePos);

			FRay Ray(RayOrigin, RayDirection, true);
			FVector NearestRayPos = Ray.ClosestPoint(NearestCirclePos);

			const double HitBuffer = PixelHitThresholdAdjust + LineThickness;
			double Distance = FVector::Distance(NearestCirclePos, NearestRayPos);
			
			if (Distance <= HitBuffer)
			{
				FInputRayHit RayHit(HitDepth);
				RayHit.SetHitObject(this);
				RayHit.HitIdentifier = PartIdentifier;
				return RayHit;
			}
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

