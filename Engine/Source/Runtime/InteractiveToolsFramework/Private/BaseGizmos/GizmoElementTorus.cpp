// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementTorus.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"
#include "SceneManagement.h"

void UGizmoElementTorus::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	FRenderTraversalState CurrentRenderState(RenderState);
	bool bVisibleViewDependent = UpdateRenderState(RenderAPI, Center, CurrentRenderState);

	if (bVisibleViewDependent)
	{
		if (const UMaterialInterface* UseMaterial = CurrentRenderState.GetCurrentMaterial())
		{
			FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();

			FVector TorusSideAxis = Normal ^ BeginAxis;
			TorusSideAxis.Normalize();

			DrawTorus(PDI, CurrentRenderState.LocalToWorldTransform.ToMatrixWithScale(), 
				BeginAxis, TorusSideAxis, OuterRadius, InnerRadius, OuterSegments, InnerSlices,  
				UseMaterial->GetRenderProxy(), SDPG_Foreground, bPartial, Angle, bEndCaps);
		}
	}
}


//
// This method approximates ray-torus intersection by intersecting the ray with the plane in which the torus 
// lies, then determining a hit point closest to the linear circle defined by torus center and torus outer radius.
// 
// If the torus lies at a glancing angle, the ray-torus intersection is performed against cylinders approximating
// the shape of the torus.
//
FInputRayHit UGizmoElementTorus::LineTrace(const UGizmoViewContext* ViewContext, const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection)
{
	FLineTraceTraversalState CurrentLineTraceState(LineTraceState);
	bool bHittableViewDependent = UpdateLineTraceState(ViewContext, Center, CurrentLineTraceState);

	if (bHittableViewDependent)
	{
		const double PixelHitThresholdAdjust = CurrentLineTraceState.PixelToWorldScale * PixelHitDistanceThreshold;
		const double WorldOuterRadius = OuterRadius * CurrentLineTraceState.LocalToWorldTransform.GetScale3D().X;
		const double WorldInnerRadius = InnerRadius * CurrentLineTraceState.LocalToWorldTransform.GetScale3D().X;
		const FVector WorldCenter = CurrentLineTraceState.LocalToWorldTransform.TransformPosition(FVector::ZeroVector);
		const FVector WorldNormal = CurrentLineTraceState.LocalToWorldTransform.TransformVectorNoScale(Normal);
		const FVector WorldBeginAxis = CurrentLineTraceState.LocalToWorldTransform.GetRotation().RotateVector(BeginAxis);
		double HitDepth = -1.0;


		// Determine if the ray direction is at a glancing angle by using a minimum cos angle based on the
		// angle between the vector from torus center to ring center and the vector from torus center to ring edge.
		double MinCosAngle = OuterRadius / FMath::Sqrt(OuterRadius * OuterRadius + InnerRadius * InnerRadius);
		bool bAtGlancingAngle = FMath::Abs(FVector::DotProduct(WorldNormal, RayDirection)) <= MinCosAngle;

		if (bAtGlancingAngle)
		{
			// If the torus lies at a glancing angle, the ray-torus intersection is performed against cylinders approximating
			// the shape of the torus.
			static constexpr int NumFullTorusCylinders = 16;
			static constexpr double AngleDelta = UE_DOUBLE_TWO_PI / static_cast<double>(NumFullTorusCylinders);
			const int NumCylinders = bPartial ? FMath::CeilToInt(NumFullTorusCylinders * Angle / UE_DOUBLE_TWO_PI) : NumFullTorusCylinders;

			const FVector ViewDirection = ViewContext->GetViewDirection();
			FVector VectorA = WorldBeginAxis; 
			FVector VectorB = VectorA.RotateAngleAxisRad(AngleDelta, WorldNormal);

			const double CylinderRadius = WorldInnerRadius + PixelHitThresholdAdjust;
			double CylinderHeight = (VectorB - VectorA).Length() * WorldOuterRadius;

			if (FMath::IsNearlyZero(CylinderHeight))
			{
				return FInputRayHit();
			}

			// Line trace against a set of cylinders approximating the shape of the torus
			for (int i = 0; i < NumCylinders; i++)
			{
				if (i > 0)
				{
					VectorA = VectorB;
					VectorB = VectorA.RotateAngleAxisRad(AngleDelta, Normal);
				}

				if (i == NumCylinders - 1)
				{
					CylinderHeight = bPartial ? FMath::Fmod(Angle, AngleDelta) * CylinderHeight : CylinderHeight;
				}

				const FVector CylinderDirection = (VectorB - VectorA).GetSafeNormal();
				const FVector CylinderCenter = WorldCenter + (VectorA * WorldOuterRadius) + CylinderDirection * CylinderHeight * 0.5;			
				bool bIntersects = false;
				double RayParam = -1.0;

				GizmoMath::RayCylinderIntersection(
					CylinderCenter,
					CylinderDirection,
					CylinderRadius,
					CylinderHeight,
					RayOrigin, RayDirection,
					bIntersects, RayParam);

				// Update with closest hit depth
				if (bIntersects && (HitDepth < 0 || HitDepth > RayParam))
				{
					HitDepth = RayParam;
				}
			}
		}
		else
		{
			// Intersect ray with plane in which torus lies.
			FPlane Plane(WorldCenter, WorldNormal);
			HitDepth = FMath::RayPlaneIntersectionParam(RayOrigin, RayDirection, Plane);
			if (HitDepth < 0.0)
			{
				return FInputRayHit();
			}

			FVector HitPoint = RayOrigin + RayDirection * HitDepth;

			// Find the closest point on the circle to the intersection point
			FVector NearestCirclePos;
			GizmoMath::ClosetPointOnCircle(HitPoint, WorldCenter, WorldNormal, WorldOuterRadius, NearestCirclePos);

			// Find the closest point on the ray to the circle and determine if it is within the torus
			FRay Ray(RayOrigin, RayDirection, true);
			FVector NearestRayPos = Ray.ClosestPoint(NearestCirclePos);

			const double HitBuffer = PixelHitThresholdAdjust + WorldInnerRadius;
			double Distance = FVector::Distance(NearestCirclePos, NearestRayPos);

			if (Distance > HitBuffer)
			{
				return FInputRayHit();
			}

			// Adjust hit depth to return the closest hit point's depth
			HitDepth = (NearestRayPos - RayOrigin).Length();
		}

		if (HitDepth >= 0.0)
		{
			FInputRayHit RayHit(HitDepth);
			RayHit.SetHitObject(this);
			RayHit.HitIdentifier = PartIdentifier;
			return RayHit;
		}
	}
	
	return FInputRayHit();
}

FBoxSphereBounds UGizmoElementTorus::CalcBounds(const FTransform& LocalToWorld) const
{
	// @todo - implement box-sphere bounds calculation
	return FBoxSphereBounds();
}

void UGizmoElementTorus::SetCenter(const FVector& InCenter)
{
	Center = InCenter;
}

FVector UGizmoElementTorus::GetCenter() const
{
	return Center;
}

void UGizmoElementTorus::SetNormal(const FVector& InNormal)
{
	Normal = InNormal;
	Normal.Normalize();
}

FVector UGizmoElementTorus::GetNormal() const
{
	return Normal;
}

void UGizmoElementTorus::SetBeginAxis(const FVector& InBeginAxis)
{
	BeginAxis = InBeginAxis;
	BeginAxis.Normalize();
}

FVector UGizmoElementTorus::GetBeginAxis() const
{
	return BeginAxis;
}

void UGizmoElementTorus::SetOuterRadius(float InOuterRadius)
{
	OuterRadius = InOuterRadius;
}

float UGizmoElementTorus::GetOuterRadius() const
{
	return OuterRadius;
}

void UGizmoElementTorus::SetInnerRadius(float InInnerRadius)
{
	InnerRadius = InInnerRadius;
}

float UGizmoElementTorus::GetInnerRadius() const
{
	return InnerRadius;
}

void UGizmoElementTorus::SetOuterSegments(int32 InOuterSegments)
{
	OuterSegments = InOuterSegments;
}

int32 UGizmoElementTorus::GetOuterSegments() const
{
	return OuterSegments;
}

void UGizmoElementTorus::SetInnerSlices(int32 InInnerSlices)
{
	InnerSlices = InInnerSlices;
}

int32 UGizmoElementTorus::GetInnerSlices() const
{
	return InnerSlices;
}

void UGizmoElementTorus::SetPartial(bool InPartial)
{
	bPartial = InPartial;
}

bool UGizmoElementTorus::GetPartial() const
{
	return bPartial;
}

void UGizmoElementTorus::SetAngle(float InAngle)
{
	Angle = InAngle;
}

float UGizmoElementTorus::GetAngle() const
{
	return Angle;
}

void UGizmoElementTorus::SetEndCaps(bool InEndCaps)
{
	bEndCaps = InEndCaps;
}

bool UGizmoElementTorus::GetEndCaps() const
{
	return bEndCaps;
}
