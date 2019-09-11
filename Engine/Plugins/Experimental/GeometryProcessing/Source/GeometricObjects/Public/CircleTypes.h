// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of WildMagic DistPoint3TCircle3

#pragma once

#include "VectorTypes.h"
#include "VectorUtil.h"
#include "BoxTypes.h"
#include "OrientedBoxTypes.h"
#include "FrameTypes.h"

template<typename RealType>
struct TCircle2
{
	FVector2<RealType> Center = FVector2<RealType>(0, 0);
	RealType Radius = (RealType)1;
	bool bIsReversed = false;

	TCircle2() {}

	explicit TCircle2(const RealType& RadiusIn)
	{
		Center = FVector2<RealType>(0, 0);
		Radius = RadiusIn;
	}

	TCircle2(const FVector2<RealType>& CenterIn, const RealType& RadiusIn)
	{
		Center = CenterIn;
		Radius = RadiusIn;
	}


	RealType GetCircumference() const
	{
		return TMathUtil<RealType>::TwoPi * Radius;
	}
	void SetCircumference(RealType NewCircumference)
	{
		Radius = NewCircumference / TMathUtil<RealType>::TwoPi;
	}


	RealType GetDiameter() const
	{
		return (RealType)2 * Radius;
	}
	void SetDiameter(RealType NewDiameter)
	{
		Radius = NewDiameter * (RealType)0.5;
	}

	RealType GetArea() const
	{
		return TMathUtil<RealType>::Pi * Radius * Radius;
	}
	void SetArea(RealType NewArea)
	{
		Radius = TMathUtil<RealType>::Sqrt(NewArea / TMathUtil<RealType>::Pi);
	}


	RealType GetCurvature() const
	{
		return (RealType)1 / Radius;
	}

	RealType GetSignedCurvature() const
	{
		return (bIsReversed) ? ((RealType)-1 / Radius) : ((RealType)1 / Radius);
	}


	FVector2<RealType> GetPointFromAngleD(RealType AngleDeg) const
	{
		return GetPointFromAngleR(AngleDeg * TMathUtil<RealType>::DegToRad);
	}

	FVector2<RealType> GetPointFromAngleR(RealType AngleRad) const
	{
		RealType c = TMathUtil<RealType>::Cos(AngleRad), s = TMathUtil<RealType>::Sin(AngleRad);
		return FVector2<RealType>(Center.X + c*Radius, Center.Y + s*Radius);
	}


	FVector2<RealType> GetPointFromUnitParameter(RealType UnitParam) const
	{
		RealType AngleRad = ((bIsReversed) ? (-UnitParam) : (UnitParam)) * TMathUtil<RealType>::TwoPi;
		return GetPointFromAngleR(AngleRad);
	}


	bool IsInside(const FVector2<RealType>& Point) const
	{
		return Center.DistanceSquared(Point) < Radius*Radius;
	}


	RealType SignedDistance(const FVector2<RealType>& Point) const
	{
		return Center.Distance(Point) - Radius;
	}

	RealType Distance(const FVector2<RealType>& Point) const
	{
		return TMathUtil<RealType>::Abs(Center.Distance(Point) - Radius);
	}


	TAxisAlignedBox2<RealType> GetBoundingBox() const
	{
		FVector2<RealType>(Center.X + Radius, Center.Y + Radius);
		return TAxisAlignedBox2<RealType>(
			FVector2<RealType>(Center.X - Radius, Center.Y - Radius),
			FVector2<RealType>(Center.X + Radius, Center.Y + Radius));
	}


	RealType GetBoundingPolygonRadius(int NumSides) const
	{
		RealType DeltaAngle = (TMathUtil<RealType>::TwoPi / (RealType)NumSides) / (RealType)2;
		return Radius / TMathUtil<RealType>::Cos(DeltaAngle);
	}

};

typedef TCircle2<float> FCircle2f;
typedef TCircle2<double> FCircle2d;







template<typename RealType>
struct TCircle3
{
	TFrame3<RealType> Frame;
	RealType Radius = (RealType)1;
	bool bIsReversed = false;

	TCircle3() {}

	explicit TCircle3(const RealType& RadiusIn)
	{
		Radius = RadiusIn;
	}

	TCircle3(const FVector3<RealType>& CenterIn, const RealType& RadiusIn)
	{
		Frame.Origin = CenterIn;
		Radius = RadiusIn;
	}

	TCircle3(const TFrame3<RealType>& FrameIn, const RealType& RadiusIn)
	{
		Frame = FrameIn;
		Radius = RadiusIn;
	}


	const FVector3<RealType>& GetCenter() const
	{
		return Frame.Origin;
	}

	FVector3<RealType> GetNormal() const
	{
		return Frame.Z();
	}


	RealType GetCircumference() const
	{
		return TMathUtil<RealType>::TwoPi * Radius;
	}
	void SetCircumference(RealType NewCircumference)
	{
		Radius = NewCircumference / TMathUtil<RealType>::TwoPi;
	}


	RealType GetDiameter() const
	{
		return (RealType)2 * Radius;
	}
	void SetDiameter(RealType NewDiameter)
	{
		Radius = NewDiameter * (RealType)0.5;
	}

	RealType GetArea() const
	{
		return TMathUtil<RealType>::Pi * Radius * Radius;
	}
	void SetArea(RealType NewArea)
	{
		Radius = TMathUtil<RealType>::Sqrt(NewArea / TMathUtil<RealType>::Pi);
	}


	RealType GetCurvature() const
	{
		return (RealType)1 / Radius;
	}

	RealType GetSignedCurvature() const
	{
		return (bIsReversed) ? ((RealType)-1 / Radius) : ((RealType)1 / Radius);
	}


	FVector3<RealType> GetPointFromAngleD(RealType AngleDeg) const
	{
		return GetPointFromAngleR(AngleDeg * TMathUtil<RealType>::DegToRad);
	}

	FVector3<RealType> GetPointFromAngleR(RealType AngleRad) const
	{
		RealType c = TMathUtil<RealType>::Cos(AngleRad), s = TMathUtil<RealType>::Sin(AngleRad);
		return Frame.FromPlaneUV(FVector2<RealType>(Radius*c, Radius*s), 2);
	}

	FVector3<RealType> GetPointFromUnitParameter(RealType UnitParam) const
	{
		RealType AngleRad = ((bIsReversed) ? (-UnitParam) : (UnitParam)) * TMathUtil<RealType>::TwoPi;
		return GetPointFromAngleR(AngleRad);
	}




	FVector3<RealType> ClosestPoint(const FVector3<RealType>& QueryPoint) const
	{
		const FVector3<RealType>& Center = Frame.Origin;
		FVector3<RealType> Normal = Frame.GetAxis(2);

		FVector3<RealType> PointDelta = QueryPoint - Center;
		FVector3<RealType> DeltaInPlane = PointDelta - Normal.Dot(PointDelta)*Normal;
		RealType OriginDist = DeltaInPlane.Length();
		if (OriginDist > (RealType)0)
		{
			return Center + (Radius / OriginDist)*DeltaInPlane;
		}
		else    // all points equidistant, use any one
		{
			return Frame.Origin + Radius * Frame.GetAxis(0);
		}
	}


	RealType DistanceSquared(const FVector3<RealType>& Point) const
	{
		return Point.DistanceSquared(ClosestPoint(Point));
	}
	
	RealType Distance(const FVector3<RealType>& Point) const
	{
		return TMathUtil<RealType>::Sqrt(DistanceSquared(Point));
	}

	TOrientedBox3<RealType> GetBoundingBox() const
	{
		return TOrientedBox3<RealType>(Frame, FVector3<RealType>(Radius, Radius, 0));
	}

};

typedef TCircle3<float> FCircle3f;
typedef TCircle3<double> FCircle3d;

