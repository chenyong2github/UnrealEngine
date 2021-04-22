// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Math/Point.h"
#include "CADKernel/Math/Geometry.h"

namespace CADKernel
{
	enum ESide 
	{
		OnPlane,
		AbovePlane,
		BelowPlane
	};

	class FPlane
	{
	private:
		FPoint Origin;
		FPoint Normal;

	public:

		FPlane(const FPoint& Point = FPoint(), const FPoint& Normal = FPoint())
			: Origin(Point)
			, Normal(Normal)
		{}

		FPoint GetPoint(double Lambda = 0.) const
		{
			return Origin + Normal * Lambda;
		}

		void SetOrigin(const FPoint& Point)
		{
			Origin = Point;
		}

		const FPoint& GetOrigin() const 
		{
			return Origin;
		}

		const FPoint& GetNormal() const
		{
			return Normal;
		}

		void SetNormal(const FPoint& InNormal)
		{
			Normal = InNormal;
		}

		FPoint PointProjection(const FPoint& Point) const
		{
			double Distance;
			return ProjectPointOnPlane(Point, Origin, Normal, Distance);
		}

		ESide GetSide(const FPoint& Point) const
		{
			double DotProduct = (Point - Origin) * Normal;
			if (DotProduct  < 0)
			{
				return ESide::BelowPlane;
			}

			if (DotProduct  > 0)
			{
				return ESide::AbovePlane;
			}

			return ESide::OnPlane;
		}

		FPlane& TranslateAlongNormal(double Step)
		{
			Origin += Normal * Step;
			return *this;
		}

		FPlane TranslateAlongNormal(double Step) const 
		{
			return FPlane(Origin + Normal * Step, Normal);
		}
	};

}

