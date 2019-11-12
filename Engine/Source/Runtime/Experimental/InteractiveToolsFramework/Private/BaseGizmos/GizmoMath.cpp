// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoMath.h"




FVector GizmoMath::ProjectPointOntoLine(
	const FVector& Point,
	const FVector& LineOrigin, const FVector& LineDirection)
{
	float ProjectionParam = FVector::DotProduct((Point - LineOrigin), LineDirection);
	return LineOrigin + ProjectionParam * LineDirection;
}


void GizmoMath::NearestPointOnLine(
	const FVector& LineOrigin, const FVector& LineDirection,
	const FVector& QueryPoint,
	FVector& NearestPointOut, float& LineParameterOut)
{
	check(LineDirection.IsNormalized());
	LineParameterOut = FVector::DotProduct( (QueryPoint - LineOrigin), LineDirection);
	NearestPointOut = LineOrigin + LineParameterOut * LineDirection;
}


void GizmoMath::NearestPointOnLineToRay(
	const FVector& LineOrigin, const FVector& LineDirection,
	const FVector& RayOrigin, const FVector& RayDirection,
	FVector& NearestLinePointOut, float& LineParameterOut,
	FVector& NearestRayPointOut, float& RayParameterOut)
{
	FVector kDiff = LineOrigin - RayOrigin;
	float a01 = -FVector::DotProduct(LineDirection, RayDirection);
	float b0 = FVector::DotProduct(kDiff, LineDirection);
	float c = kDiff.SizeSquared();
	float det = FMath::Abs((float)1 - a01 * a01);
	float b1, s0, s1;

	if (det >= SMALL_NUMBER)
	{
		b1 = -FVector::DotProduct(kDiff, RayDirection);
		s1 = a01 * b0 - b1;

		if (s1 >= (float)0)
		{
			// Two interior points are closest, one on Line and one on Ray
			float invDet = ((float)1) / det;
			s0 = (a01 * b1 - b0) * invDet;
			s1 *= invDet;
		}
		else
		{
			// Origin of Ray and interior point of Line are closest.
			s0 = -b0;
			s1 = (float)0;
		}
	}
	else
	{
		// Lines are parallel, closest pair with one point at Ray origin.
		s0 = -b0;
		s1 = (float)0;
	}

	NearestLinePointOut = LineOrigin + s0 * LineDirection;
	NearestRayPointOut = RayOrigin + s1 * RayDirection;
	LineParameterOut = s0;
	RayParameterOut = s1;
}




void GizmoMath::RayPlaneIntersectionPoint(
	const FVector& PlaneOrigin, const FVector& PlaneNormal,
	const FVector& RayOrigin, const FVector& RayDirection,
	bool& bIntersectsOut, FVector& PlaneIntersectionPointOut)
{
	bIntersectsOut = false;
	PlaneIntersectionPointOut = PlaneOrigin;

	float PlaneEquationD = -FVector::DotProduct(PlaneOrigin, PlaneNormal);
	float NormalDot = FVector::DotProduct(RayDirection, PlaneNormal);

	if (FMath::Abs(NormalDot) < SMALL_NUMBER)
	{
		return;
	}

	float RayParam = -( FVector::DotProduct(RayOrigin, PlaneNormal) + PlaneEquationD) / NormalDot;
	if (RayParam < 0)
	{
		return;
	}

	PlaneIntersectionPointOut = RayOrigin + RayParam * RayDirection;
	bIntersectsOut = true;
}


void GizmoMath::RaySphereIntersection(
	const FVector& SphereOrigin, const float SphereRadius,
	const FVector& RayOrigin, const FVector& RayDirection,
	bool& bIntersectsOut, FVector& SphereIntersectionPointOut)
{
	bIntersectsOut = false;
	SphereIntersectionPointOut = RayOrigin;
	 
	FVector DeltaPos = RayOrigin - SphereOrigin;
	float a0 = DeltaPos.SizeSquared() - SphereRadius*SphereRadius;
	float a1 = FVector::DotProduct(RayDirection, DeltaPos);
	float discr = a1 * a1 - a0;
	if (discr > 0)   // intersection only when roots are real
	{
		bIntersectsOut = true;
		float root = FMath::Sqrt(discr);
		float NearRayParam = -a1 + root;		// isn't it always this one?
		float NearRayParam2 = -a1 - root;
		float UseRayParam = FMath::Min(NearRayParam, NearRayParam2);
		SphereIntersectionPointOut = RayOrigin + UseRayParam * RayDirection;
	}
}



void GizmoMath::ClosetPointOnCircle(
	const FVector& QueryPoint,
	const FVector& CircleOrigin, const FVector& CircleNormal, float CircleRadius,
	FVector& ClosestPointOut)
{
	FVector PointDelta = QueryPoint - CircleOrigin;
	FVector DeltaInPlane = PointDelta - FVector::DotProduct(CircleNormal,PointDelta)*CircleNormal;
	float OriginDist = DeltaInPlane.Size();
	if (OriginDist > 0.0f)
	{
		ClosestPointOut =  CircleOrigin + (CircleRadius / OriginDist) * DeltaInPlane;
	}
	else    // all points equidistant, use any one
	{
		FVector PlaneX, PlaneY;
		MakeNormalPlaneBasis(CircleNormal, PlaneX, PlaneY);
		ClosestPointOut = CircleOrigin + CircleRadius * PlaneX;
	}
}



void GizmoMath::MakeNormalPlaneBasis(
	const FVector& PlaneNormal,
	FVector& BasisAxis1Out, FVector& BasisAxis2Out)
{
	// Duff et al method, from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
	if (PlaneNormal.Z < 0)
	{
		float A = 1.0f / (1.0f - PlaneNormal.Z);
		float B = PlaneNormal.X * PlaneNormal.Y * A;
		BasisAxis1Out.X = 1.0f - PlaneNormal.X * PlaneNormal.X * A;
		BasisAxis1Out.Y = -B;
		BasisAxis1Out.Z = PlaneNormal.X;
		BasisAxis2Out.X = B;
		BasisAxis2Out.Y = PlaneNormal.Y * PlaneNormal.Y * A - 1.0f;
		BasisAxis2Out.Z = -PlaneNormal.Y;
	}
	else
	{
		float A = 1.0f / (1.0f + PlaneNormal.Z);
		float B = -PlaneNormal.X * PlaneNormal.Y * A;
		BasisAxis1Out.X = 1.0f - PlaneNormal.X * PlaneNormal.X * A;
		BasisAxis1Out.Y = B;
		BasisAxis1Out.Z = -PlaneNormal.X;
		BasisAxis2Out.X = B;
		BasisAxis2Out.Y = 1.0f - PlaneNormal.Y * PlaneNormal.Y * A;
		BasisAxis2Out.Z = -PlaneNormal.Y;
	}
}



float GizmoMath::ComputeAngleInPlane(
	const FVector& Point,
	const FVector& PlaneOrigin, const FVector& PlaneNormal,
	const FVector& PlaneAxis1, const FVector& PlaneAxis2)
{
	// project point into plane
	FVector LocalPoint = Point - PlaneOrigin;

	float X = FVector::DotProduct(LocalPoint, PlaneAxis1);
	float Y = FVector::DotProduct(LocalPoint, PlaneAxis2);

	float SignedAngle = (float)atan2(Y, X);
	return SignedAngle;
}




FVector2D GizmoMath::ComputeCoordinatesInPlane(
	const FVector& Point,
	const FVector& PlaneOrigin, const FVector& PlaneNormal,
	const FVector& PlaneAxis1, const FVector& PlaneAxis2)
{
	FVector LocalPoint = Point - PlaneOrigin;
	float X = FVector::DotProduct(LocalPoint, PlaneAxis1);
	float Y = FVector::DotProduct(LocalPoint, PlaneAxis2);
	return FVector2D(X, Y);
}


FVector GizmoMath::ProjectPointOntoPlane(
	const FVector& Point,
	const FVector& PlaneOrigin, const FVector& PlaneNormal)
{
	FVector LocalPoint = Point - PlaneOrigin;
	float NormalDot = FVector::DotProduct(LocalPoint, PlaneNormal);
	return Point - NormalDot * PlaneNormal;
}



float GizmoMath::SnapToIncrement(float Value, float Increment)
{
	if (!FMath::IsFinite(Value))
	{
		return 0;
	}
	float Sign = FMath::Sign(Value);
	Value = FMath::Abs(Value);
	int IntIncrement = (int)(Value / Increment);
	float Remainder = (float)fmod(Value, Increment);
	if (Remainder > IntIncrement / 2)
	{
		++IntIncrement;
	}
	return Sign * (float)IntIncrement * Increment;
}