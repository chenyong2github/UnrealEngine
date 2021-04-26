// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Math/Point.h"


enum EAABBBoundary : uint32
{
	XMax = 0x00000000u,
	YMax = 0x00000000u,
	ZMax = 0x00000000u,
	XMin = 0x00000001u,
	YMin = 0x00000002u,
	ZMin = 0x00000004u,
};

ENUM_CLASS_FLAGS(EAABBBoundary);

namespace CADKernel
{
	class FSphere;
	class FPlane;

	class FAABB
	{

	private:
		FPoint MinCorner;
		FPoint MaxCorner;

	public:
		FAABB()
			: MinCorner(HUGE_VALUE, HUGE_VALUE, HUGE_VALUE)
			, MaxCorner(-HUGE_VALUE, -HUGE_VALUE, -HUGE_VALUE)
		{
		}

		FAABB(const FPoint& InMinCorner, const FPoint& InMaxCorner)
			: MinCorner(InMinCorner)
			, MaxCorner(InMaxCorner)
		{
		}

		bool IsValid() const
		{
			return (MinCorner.X < MaxCorner.Y);
		}

		void Empty()
		{
			MinCorner.Set(HUGE_VALUE, HUGE_VALUE, HUGE_VALUE);
			MaxCorner.Set(-HUGE_VALUE, -HUGE_VALUE, -HUGE_VALUE);
		}

		bool Contains(const FPoint& Point) const
		{
			return ((Point.X > MinCorner.X - SMALL_NUMBER) && (Point.X < MaxCorner.X + SMALL_NUMBER) &&
				(Point.Y > MinCorner.Y - SMALL_NUMBER) && (Point.Y < MaxCorner.Y + SMALL_NUMBER) &&
				(Point.Z > MinCorner.Z - SMALL_NUMBER) && (Point.Z < MaxCorner.Z + SMALL_NUMBER));
		}

		void SetMinDimension(double MinDimension)
		{
			for (int32 Axis = 0; Axis < 3; Axis++)
			{
				double AxisDimension = GetDimension(Axis);
				if (AxisDimension < MinDimension)
				{
					double Offset = (MinDimension - AxisDimension) / 2;
					MinCorner[Axis] -= Offset;
					MaxCorner[Axis] += Offset;
				}
			}
		}

		double GetMaxDimension() const
		{
			double MaxDimension = 0;
			for (int32 Index = 0; Index < 3; Index++)
			{
				double Dimension = GetDimension(Index);
				if (Dimension > MaxDimension)
				{
					MaxDimension = Dimension;
				}
			}
			return MaxDimension;
		}

		double GetDimension(int32 Axis) const
		{
			return MaxCorner[Axis] - MinCorner[Axis];
		}

		bool Contains(const FAABB& Aabb) const
		{
			return IsValid() && Aabb.IsValid() && Contains(Aabb.MinCorner) && Contains(Aabb.MaxCorner);
		}

		const FPoint& GetMin() const
		{
			return MinCorner;
		}

		const FPoint& GetMax() const
		{
			return MaxCorner;
		}

		FPoint GetCorner(int32 Corner) const
		{
			return FPoint(
				Corner & EAABBBoundary::XMin ? MinCorner[0] : MaxCorner[0],
				Corner & EAABBBoundary::YMin ? MinCorner[1] : MaxCorner[1],
				Corner & EAABBBoundary::ZMin ? MinCorner[2] : MaxCorner[2]
			);
		}

		FAABB& operator+= (const double* Point)
		{
			for (int32 Index = 0; Index < 3; Index++)
			{
				if (Point[Index] < MinCorner[Index])
				{
					MinCorner[Index] = Point[Index];
				}
				if (Point[Index] > MaxCorner[Index])
				{
					MaxCorner[Index] = Point[Index];
				}
			}
			return *this;
		}

		FAABB& operator+= (const FPoint& Point)
		{
			for (int32 Index = 0; Index < 3; Index++)
			{
				if (Point[Index] < MinCorner[Index])
				{
					MinCorner[Index] = Point[Index];
				}
				if (Point[Index] > MaxCorner[Index])
				{
					MaxCorner[Index] = Point[Index];
				}
			}
			return *this;
		}

		void Offset (double Offset)
		{
			for (int32 Index = 0; Index < 3; Index++)
			{
				MinCorner[Index] -= Offset;
				MaxCorner[Index] += Offset;
			}
		}

		FAABB& operator+= (const FAABB& aabb)
		{
			*this += aabb.MinCorner;
			*this += aabb.MaxCorner;
			return *this;
		}

		FAABB operator+ (const FPoint& Point) const
		{
			FAABB Other = *this;
			Other += Point;
			return Other;
		}


		FAABB operator+ (const FAABB& Aabb) const
		{
			FAABB Other = *this;
			Other += Aabb;
			return Other;
		}

	};

	class CADKERNEL_API FAABB2D
	{
	private:

		FPoint2D MinCorner;
		FPoint2D MaxCorner;

	public:

		FAABB2D()
			: MinCorner(HUGE_VALUE, HUGE_VALUE)
			, MaxCorner(-HUGE_VALUE, -HUGE_VALUE)
		{
		}

		FAABB2D(const FPoint2D& InMinCorner, const FPoint2D& InMaxCorner)
			: MinCorner(InMinCorner)
			, MaxCorner(InMaxCorner)
		{
		}

		friend FArchive& operator<<(FArchive& Ar, FAABB2D& AABB)
		{
			Ar << AABB.MinCorner;
			Ar << AABB.MaxCorner;
			return Ar;
		}

		const FPoint2D& GetMin() const
		{
			return MinCorner;
		}

		const FPoint2D& GetMax() const
		{
			return MaxCorner;
		}

		bool IsValid() const
		{
			return (MinCorner.U < MaxCorner.U);
		}

		void Empty()
		{
			MinCorner.Set(HUGE_VALUE, HUGE_VALUE);
			MaxCorner.Set(-HUGE_VALUE, -HUGE_VALUE);
		}

		bool Contains(const FPoint2D& Point) const
		{
			return ((Point.U > MinCorner.U - SMALL_NUMBER) && (Point.U < MaxCorner.U + SMALL_NUMBER) &&
				(Point.V > MinCorner.V - SMALL_NUMBER) && (Point.V < MaxCorner.V + SMALL_NUMBER));
		}

		FPoint2D GetCorner(int32 CornerIndex) const
		{
			return FPoint2D(
				CornerIndex & EAABBBoundary::XMin ? MinCorner[0] : MaxCorner[0],
				CornerIndex & EAABBBoundary::YMin ? MinCorner[1] : MaxCorner[1]
			);
		}

		bool Contains(const FAABB2D& Aabb2d) const
		{
			return IsValid() && Aabb2d.IsValid() && Contains(Aabb2d.MinCorner) && Contains(Aabb2d.MaxCorner);
		}

		double DiagonalLength() const
		{
			return MinCorner.Distance(MaxCorner);
		}

		FPoint2D Diagonal() const
		{
			return MaxCorner - MinCorner;
		}

		double GetDimension(int32 Axis) const
		{
			return MaxCorner[Axis] - MinCorner[Axis];
		}

		FPoint2D Center() const
		{
			return (MinCorner + MaxCorner) / 2.;
		}

		void Set(const FPoint2D& InMinCorner, const FPoint2D& InMaxCorner)
		{
			MinCorner = InMinCorner;
			MaxCorner = InMaxCorner;
		}

		FAABB2D& operator+= (const FAABB2D& Aabb2d)
		{
			*this += Aabb2d.GetMin();
			*this += Aabb2d.GetMax();
			return *this;
		}

		FAABB2D& operator+= (TArray<FPoint2D> Points)
		{
			for (FPoint Point : Points)
			{
				*this += Point;
			}
			return *this;
		}

		FAABB2D& operator+= (const FPoint2D& Point)
		{
			if (Point.U < MinCorner.U)
			{
				MinCorner.U = Point.U;
			}

			if (Point.V < MinCorner.V)
			{
				MinCorner.V = Point.V;
			}

			if (Point.U > MaxCorner.U)
			{
				MaxCorner.U = Point.U;
			}

			if (Point.V > MaxCorner.V)
			{
				MaxCorner.V = Point.V;
			}

			return *this;
		}

		void Offset(double Offset)
		{
			MinCorner.U -= Offset;
			MinCorner.V -= Offset;
			MaxCorner.U += Offset;
			MaxCorner.V += Offset;
		}
	};

} // namespace CADKernel

