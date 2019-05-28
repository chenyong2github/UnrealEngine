// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Box.h"
#include "Math/Box2D.h"
#include "VectorTypes.h"

template <typename RealType>
struct FInterval1
{
	RealType Min;
	RealType Max;

	FInterval1()
	{
	}
	FInterval1(const RealType& Min, const RealType& Max)
	{
		this->Min = Min;
		this->Max = Max;
	}

	static FInterval1<RealType> Empty()
	{
		return FInterval1(TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max());
	}

	RealType Center() const
	{
		return (Min + Max) * (RealType)0.5;
	}

	RealType Extent() const
	{
		return (Max - Min)*(RealType).5;
	}
	RealType Length() const
	{
		return Max - Min;
	}

	void Contain(const RealType& V)
	{
		if (V < Min)
		{
			Min = V;
		}
		if (V > Max)
		{
			Max = V;
		}
	}

	bool Contains(RealType D) const
	{
		return D >= Min && D <= Max;
	}

	bool Overlaps(const FInterval1<RealType>& O) const
	{
		return !(O.Min > Max || O.Max < Min);
	}

	RealType SquaredDist(const FInterval1<RealType>& O) const
	{
		if (Max < O.Min)
		{
			return (O.Min - Max) * (O.Min - Max);
		}
		else if (Min > O.Max)
		{
			return (Min - O.Max) * (Min - O.Max);
		}
		else
		{
			return 0;
		}
	}
	RealType Dist(const FInterval1<RealType>& O) const
	{
		if (Max < O.Min)
		{
			return O.Min - Max;
		}
		else if (Min > O.Max)
		{
			return Min - O.Max;
		}
		else
		{
			return 0;
		}
	}

	FInterval1<RealType> IntersectionWith(const FInterval1<RealType>& O) const
	{
		if (O.Min > Max || O.Max < Min)
		{
			return FInterval1<RealType>::Empty();
		}
		return FInterval1<RealType>(TMathUtil<RealType>::Max(Min, O.Min), TMathUtil<RealType>::Min(Max, O.Max));
	}

	/**
	 * clamp Value f to interval [Min,Max]
	 */
	RealType Clamp(RealType f) const
	{
		return (f < Min) ? Min : (f > Max) ? Max : f;
	}

	/**
	 * interpolate between Min and Max using Value T in range [0,1]
	 */
	RealType Interpolate(RealType T) const
	{
		return (1 - T) * Min + (T)*Max;
	}

	/**
	 * Convert Value into (clamped) T Value in range [0,1]
	 */
	RealType GetT(RealType Value) const
	{
		if (Value <= Min)
		{
			return 0;
		}
		else if (Value >= Max)
		{
			return 1;
		}
		else if (Min == Max)
		{
			return 0.5;
		}
		else
		{
			return (Value - Min) / (Max - Min);
		}
	}

	void Set(FInterval1 O)
	{
		Min = O.Min;
		Max = O.Max;
	}

	void Set(RealType A, RealType B)
	{
		Min = A;
		Max = B;
	}

	FInterval1 operator-(FInterval1 V) const
	{
		return FInterval1(-V.Min, -V.Max);
	}

	FInterval1 operator+(RealType f) const
	{
		return FInterval1(Min + f, Max + f);
	}

	FInterval1 operator-(RealType f) const
	{
		return FInterval1(Min - f, Max - f);
	}

	FInterval1 operator*(RealType f) const
	{
		return FInterval1(Min * f, Max * f);
	}
};

typedef FInterval1<float> FInterval1f;
typedef FInterval1<double> FInterval1d;
typedef FInterval1<int> FInterval1i;

template <typename RealType>
struct FAxisAlignedBox3
{
	FVector3<RealType> Min;
	FVector3<RealType> Max;

	FAxisAlignedBox3()
	{
	}
	FAxisAlignedBox3(const FVector3<RealType>& Min, const FVector3<RealType>& Max)
	{
		this->Min = Min;
		this->Max = Max;
	}
	FAxisAlignedBox3(const FAxisAlignedBox3& Box, const TFunction<FVector3<RealType>(const FVector3<RealType>&)> TransformF)
	{
		if (TransformF == nullptr)
		{
			Min = Box.Min;
			Max = Box.Max;
			return;
		}

		FVector3<RealType> C0 = TransformF(Box.GetCorner(0));
		Min = C0;
		Max = C0;
		for (int i = 1; i < 8; ++i)
		{
			Contain(TransformF(Box.GetCorner(i)));
		}
	}

	operator FBox() const
	{
		FVector MinV((float)Min.X, (float)Min.Y, (float)Min.Z);
		FVector MaxV((float)Max.X, (float)Max.Y, (float)Max.Z);
		return FBox(MinV, MaxV);
	}
	FAxisAlignedBox3(const FBox& Box)
	{
		Min = Box.Min;
		Max = Box.Max;
	}

	/**
	* @param Index corner index in range 0-7
	* @return Corner point on the box identified by the given index. See diagram in OrientedBoxTypes.h for index/corner mapping.
	*/
	FVector3<RealType> GetCorner(int Index) const
	{
		check(Index >= 0 && Index <= 7);
		double X = (((Index & 1) != 0) ^ ((Index & 2) != 0)) ? (Max.X) : (Min.X);
		double Y = ((Index / 2) % 2 == 0) ? (Min.Y) : (Max.Y);
		double Z = (Index < 4) ? (Min.Z) : (Max.Z);
		return FVector3<RealType>(X, Y, Z);
	}

	static FAxisAlignedBox3<RealType> Empty()
	{
		return FAxisAlignedBox3(
			FVector3<RealType>(TNumericLimits<RealType>::Max(), TNumericLimits<RealType>::Max(), TNumericLimits<RealType>::Max()),
			FVector3<RealType>(-TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max()));
	}

	FVector3<RealType> Center() const
	{
		return FVector3<RealType>(
			(Min.X + Max.X) * (RealType)0.5,
			(Min.Y + Max.Y) * (RealType)0.5,
			(Min.Z + Max.Z) * (RealType)0.5);
	}

	FVector3<RealType> Extents() const
	{
		return (Max - Min) * (RealType).5;
	}

	void Contain(const FVector3<RealType>& V)
	{
		if (V.X < Min.X)
		{
			Min.X = V.X;
		}
		if (V.X > Max.X)
		{
			Max.X = V.X;
		}
		if (V.Y < Min.Y)
		{
			Min.Y = V.Y;
		}
		if (V.Y > Max.Y)
		{
			Max.Y = V.Y;
		}
		if (V.Z < Min.Z)
		{
			Min.Z = V.Z;
		}
		if (V.Z > Max.Z)
		{
			Max.Z = V.Z;
		}
	}

	void Contain(const FAxisAlignedBox3<RealType>& Other)
	{
		// todo: can be optimized
		Contain(Other.Min);
		Contain(Other.Max);
	}

	bool Contains(const FVector3<RealType>& V) const
	{
		return (Min.X <= V.X) && (Min.Y <= V.Y) && (Min.Z <= V.Z) && (Max.X >= V.X) && (Max.Y >= V.Y) && (Max.Z >= V.Z);
	}

	FAxisAlignedBox3<RealType> Intersect(const FAxisAlignedBox3<RealType>& Box) const
	{
		FAxisAlignedBox3<RealType> Intersection(
			FVector3<RealType>(TMathUtil<RealType>::Max(Min.X, Box.Min.X), TMathUtil<RealType>::Max(Min.Y, Box.Min.Y), TMathUtil<RealType>::Max(Min.Z, Box.Min.Z)),
			FVector3<RealType>(TMathUtil<RealType>::Min(Max.X, Box.Max.X), TMathUtil<RealType>::Min(Max.Y, Box.Max.Y), TMathUtil<RealType>::Min(Max.Z, Box.Max.Z)));
		if (Intersection.Height() <= 0 || Intersection.Width() <= 0 || Intersection.Depth() <= 0)
		{
			return FAxisAlignedBox3<RealType>::Empty();
		}
		else
		{
			return Intersection;
		}
	}

	bool Intersects(FAxisAlignedBox3 Box) const
	{
		return !((Box.Max.X <= Min.X) || (Box.Min.X >= Max.X) || (Box.Max.Y <= Min.Y) || (Box.Min.Y >= Max.Y) || (Box.Max.Z <= Min.Z) || (Box.Min.Z >= Max.Z));
	}

	RealType DistanceSquared(const FVector3<RealType>& V) const
	{
		RealType dx = (V.X < Min.X) ? Min.X - V.X : (V.X > Max.X ? V.X - Max.X : 0);
		RealType dy = (V.Y < Min.Y) ? Min.Y - V.Y : (V.Y > Max.Y ? V.Y - Max.Y : 0);
		RealType dz = (V.Z < Min.Z) ? Min.Z - V.Z : (V.Z > Max.Z ? V.Z - Max.Z : 0);
		return dx * dx + dy * dy + dz * dz;
	}

	RealType DistanceSquared(const FAxisAlignedBox3<RealType>& Box)
	{
		// compute lensqr( max(0, abs(center1-center2) - (extent1+extent2)) )
		RealType delta_x = TMathUtil<RealType>::Abs((Box.Min.X + Box.Max.X) - (Min.X + Max.X))
			- ((Max.X - Min.X) + (Box.Max.X - Box.Min.X));
		if (delta_x < 0)
		{
			delta_x = 0;
		}
		RealType delta_y = TMathUtil<RealType>::Abs((Box.Min.Y + Box.Max.Y) - (Min.Y + Max.Y))
			- ((Max.Y - Min.Y) + (Box.Max.Y - Box.Min.Y));
		if (delta_y < 0)
		{
			delta_y = 0;
		}
		RealType delta_z = TMathUtil<RealType>::Abs((Box.Min.Z + Box.Max.Z) - (Min.Z + Max.Z))
			- ((Max.Z - Min.Z) + (Box.Max.Z - Box.Min.Z));
		if (delta_z < 0)
		{
			delta_z = 0;
		}
		return (RealType)0.25 * (delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
	}

	RealType Width() const
	{
		return TMathUtil<RealType>::Max(Max.X - Min.X, (RealType)0);
	}

	RealType Height() const
	{
		return TMathUtil<RealType>::Max(Max.Y - Min.Y, (RealType)0);
	}

	RealType Depth() const
	{
		return TMathUtil<RealType>::Max(Max.Z - Min.Z, (RealType)0);
	}

	RealType Volume() const
	{
		return Width() * Height() * Depth();
	}

	RealType DiagonalLength() const
	{
		return TMathUtil<RealType>::Sqrt((Max.X - Min.X) * (Max.X - Min.X) + (Max.Y - Min.Y) * (Max.Y - Min.Y) + (Max.Z - Min.Z) * (Max.Z - Min.Z));
	}

	RealType MaxDim() const
	{
		return TMathUtil<RealType>::Max(Width(), TMathUtil<RealType>::Max(Height(), Depth()));
	}

	FVector3<RealType> Diagonal() const
	{
		return FVector3<RealType>(Max.X - Min.X, Max.Y - Min.Y, Max.Z - Min.Z);
	}
};

template <typename RealType>
struct FAxisAlignedBox2
{
	FVector2<RealType> Min;
	FVector2<RealType> Max;

	FAxisAlignedBox2()
	{
	}
	FAxisAlignedBox2(const FVector2<RealType>& Min, const FVector2<RealType>& Max)
		: Min(Min), Max(Max)
	{
	}
	FAxisAlignedBox2(RealType SquareSize)
		: Min((RealType)0, (RealType)0), Max(SquareSize, SquareSize)
	{
	}
	FAxisAlignedBox2(RealType Width, RealType Height)
		: Min((RealType)0, (RealType)0), Max(Width, Height)
	{
	}

	operator FBox2D() const
	{
		FVector2D MinV((float)Min.X, (float)Min.Y);
		FVector2D MaxV((float)Max.X, (float)Max.Y);
		return FBox2D(MinV, MaxV);
	}
	FAxisAlignedBox2(const FBox2D& Box)
	{
		Min = Box.Min;
		Max = Box.Max;
	}

	static FAxisAlignedBox2<RealType> Empty()
	{
		return FAxisAlignedBox2(
			FVector2<RealType>(TNumericLimits<RealType>::Max(), TNumericLimits<RealType>::Max()),
			FVector2<RealType>(-TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max()));
	}

	FVector2<RealType> Center() const
	{
		return FVector2<RealType>(
			(Min.X + Max.X) * (RealType)0.5,
			(Min.Y + Max.Y) * (RealType)0.5);
	}

	FVector2<RealType> Extents() const
	{
		return (Max - Min) * (RealType).5;
	}

	inline void Contain(const FVector2<RealType>& V)
	{
		if (V.X < Min.X)
		{
			Min.X = V.X;
		}
		if (V.X > Max.X)
		{
			Max.X = V.X;
		}
		if (V.Y < Min.Y)
		{
			Min.Y = V.Y;
		}
		if (V.Y > Max.Y)
		{
			Max.Y = V.Y;
		}
	}

	inline void Contain(const FAxisAlignedBox2<RealType>& Other)
	{
		// todo: can be optimized
		Contain(Other.Min);
		Contain(Other.Max);
	}

	void Contain(const TArray<FVector2<RealType>>& Pts)
	{
		for (const FVector2<RealType>& Pt : Pts)
		{
			Contain(Pt);
		}
	}

	bool Contains(const FVector2<RealType>& V) const
	{
		return (Min.X <= V.X) && (Min.Y <= V.Y) && (Max.X >= V.X) && (Max.Y >= V.Y);
	}

	bool Intersects(const FAxisAlignedBox2<RealType>& Box) const
	{
		return !((Box.Max.X < Min.X) || (Box.Min.X > Max.X) || (Box.Max.Y < Min.Y) || (Box.Min.Y > Max.Y));
	}

	FAxisAlignedBox2<RealType> Intersect(const FAxisAlignedBox2<RealType> &Box) const
	{
		FAxisAlignedBox2<RealType> Intersection(
			FVector2<RealType>(TMathUtil<RealType>::Max(Min.X, Box.Min.X), TMathUtil<RealType>::Max(Min.Y, Box.Min.Y)),
			FVector2<RealType>(TMathUtil<RealType>::Min(Max.X, Box.Max.X), TMathUtil<RealType>::Min(Max.Y, Box.Max.Y)));
		if (Intersection.Height() <= 0 || Intersection.Width() <= 0)
		{
			return FAxisAlignedBox2<RealType>::Empty();
		}
		else
		{
			return Intersection;
		}
	}

	RealType DistanceSquared(const FVector2<RealType>& V) const
	{
		RealType dx = (V.X < Min.X) ? Min.X - V.X : (V.X > Max.X ? V.X - Max.X : 0);
		RealType dy = (V.Y < Min.Y) ? Min.Y - V.Y : (V.Y > Max.Y ? V.Y - Max.Y : 0);
		return dx * dx + dy * dy;
	}

	inline RealType Width() const
	{
		return TMathUtil<RealType>::Max(Max.X - Min.X, (RealType)0);
	}

	inline RealType Height() const
	{
		return TMathUtil<RealType>::Max(Max.Y - Min.Y, (RealType)0);
	}

	inline RealType Area() const
	{
		return Width() * Height();
	}

	RealType DiagonalLength() const
	{
		return (RealType)TMathUtil<RealType>::Sqrt((Max.X - Min.X) * (Max.X - Min.X) + (Max.Y - Min.Y) * (Max.Y - Min.Y));
	}

	inline RealType MaxDim() const
	{
		return TMathUtil<RealType>::Max(Width(), Height());
	}

	inline RealType MinDim() const
	{
		return TMathUtil<RealType>::Min(Width(), Height());
	}
};

typedef FAxisAlignedBox2<float> FAxisAlignedBox2f;
typedef FAxisAlignedBox2<double> FAxisAlignedBox2d;
typedef FAxisAlignedBox2<int> FAxisAlignedBox2i;
typedef FAxisAlignedBox3<float> FAxisAlignedBox3f;
typedef FAxisAlignedBox3<double> FAxisAlignedBox3d;
typedef FAxisAlignedBox3<int> FAxisAlignedBox3i;
