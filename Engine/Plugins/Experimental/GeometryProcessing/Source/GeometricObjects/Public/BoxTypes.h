// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Box.h"
#include "Math/Box2D.h"
#include "VectorTypes.h"
#include "TransformTypes.h"

template <typename RealType>
struct TInterval1
{
	RealType Min;
	RealType Max;

	TInterval1() :
		TInterval1(Empty())
	{
	}

	TInterval1(const RealType& Min, const RealType& Max)
	{
		this->Min = Min;
		this->Max = Max;
	}

	static TInterval1<RealType> Empty()
	{
		return TInterval1(TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max());
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

	RealType MaxAbsExtrema() const
	{
		return TMathUtil<RealType>::Max(TMathUtil<RealType>::Abs(Min), TMathUtil<RealType>::Abs(Max));
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

	bool Contains(const TInterval1<RealType>& O) const
	{
		return Contains(O.Min) && Contains(O.Max);
	}

	bool Overlaps(const TInterval1<RealType>& O) const
	{
		return !(O.Min > Max || O.Max < Min);
	}

	RealType SquaredDist(const TInterval1<RealType>& O) const
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
	RealType Dist(const TInterval1<RealType>& O) const
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

	TInterval1<RealType> IntersectionWith(const TInterval1<RealType>& O) const
	{
		if (O.Min > Max || O.Max < Min)
		{
			return TInterval1<RealType>::Empty();
		}
		return TInterval1<RealType>(TMathUtil<RealType>::Max(Min, O.Min), TMathUtil<RealType>::Min(Max, O.Max));
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

	void Set(TInterval1 O)
	{
		Min = O.Min;
		Max = O.Max;
	}

	void Set(RealType A, RealType B)
	{
		Min = A;
		Max = B;
	}

	TInterval1 operator-(TInterval1 V) const
	{
		return TInterval1(-V.Min, -V.Max);
	}

	TInterval1 operator+(RealType f) const
	{
		return TInterval1(Min + f, Max + f);
	}

	TInterval1 operator-(RealType f) const
	{
		return TInterval1(Min - f, Max - f);
	}

	TInterval1 operator*(RealType f) const
	{
		return TInterval1(Min * f, Max * f);
	}

	inline bool IsEmpty() const
	{
		return Max < Min;
	}

	void Expand(RealType Radius)
	{
		Max += Radius;
		Min -= Radius;
	}
};

typedef TInterval1<float> FInterval1f;
typedef TInterval1<double> FInterval1d;
typedef TInterval1<int> FInterval1i;



template <typename RealType>
struct TAxisAlignedBox3
{
	FVector3<RealType> Min;
	FVector3<RealType> Max;

	TAxisAlignedBox3() : 
		TAxisAlignedBox3(TAxisAlignedBox3<RealType>::Empty())
	{
	}

	TAxisAlignedBox3(const FVector3<RealType>& Min, const FVector3<RealType>& Max)
	{
		this->Min = Min;
		this->Max = Max;
	}

	TAxisAlignedBox3(const FVector3<RealType>& A, const FVector3<RealType>& B, const FVector3<RealType>& C)
	{
		// TMathUtil::MinMax could be used here, but it generates worse code because the Min3's below will be
		// turned into SSE instructions by the optimizer, while MinMax will not
		Min = FVector3<RealType>(
			TMathUtil<RealType>::Min3(A.X, B.X, C.X),
			TMathUtil<RealType>::Min3(A.Y, B.Y, C.Y),
			TMathUtil<RealType>::Min3(A.Z, B.Z, C.Z));
		Max = FVector3<RealType>(
			TMathUtil<RealType>::Max3(A.X, B.X, C.X),
			TMathUtil<RealType>::Max3(A.Y, B.Y, C.Y),
			TMathUtil<RealType>::Max3(A.Z, B.Z, C.Z));
	}

	TAxisAlignedBox3(const TAxisAlignedBox3& OtherBox) = default;

	template<typename OtherRealType>
	explicit TAxisAlignedBox3(const TAxisAlignedBox3<OtherRealType>& OtherBox)
	{
		this->Min = FVector3<RealType>(OtherBox.Min);
		this->Max = FVector3<RealType>(OtherBox.Max);
	}

	TAxisAlignedBox3(const FVector3<RealType>& Center, RealType HalfWidth)
	{
		this->Min = FVector3<RealType>(Center.X-HalfWidth, Center.Y-HalfWidth, Center.Z-HalfWidth);
		this->Max = FVector3<RealType>(Center.X+HalfWidth, Center.Y+HalfWidth, Center.Z+HalfWidth);
	}


	TAxisAlignedBox3(const TAxisAlignedBox3& Box, const TFunction<FVector3<RealType>(const FVector3<RealType>&)> TransformF)
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

	TAxisAlignedBox3(const TAxisAlignedBox3& Box, const FTransform3d& Transform)
	{
		FVector3<RealType> C0 = Transform.TransformPosition(Box.GetCorner(0));
		Min = C0;
		Max = C0;
		for (int i = 1; i < 8; ++i)
		{
			Contain(Transform.TransformPosition(Box.GetCorner(i)));
		}
	}

	bool operator==(const TAxisAlignedBox3<RealType>& Other) const
	{
		return Max == Other.Max && Min == Other.Min;
	}
	bool operator!=(const TAxisAlignedBox3<RealType>& Other) const
	{
		return Max != Other.Max || Min != Other.Min;
	}


	explicit operator FBox() const
	{
		FVector MinV((float)Min.X, (float)Min.Y, (float)Min.Z);
		FVector MaxV((float)Max.X, (float)Max.Y, (float)Max.Z);
		return FBox(MinV, MaxV);
	}
	TAxisAlignedBox3(const FBox& Box)
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
		RealType X = (((Index & 1) != 0) ^ ((Index & 2) != 0)) ? (Max.X) : (Min.X);
		RealType Y = ((Index / 2) % 2 == 0) ? (Min.Y) : (Max.Y);
		RealType Z = (Index < 4) ? (Min.Z) : (Max.Z);
		return FVector3<RealType>(X, Y, Z);
	}

	static TAxisAlignedBox3<RealType> Empty()
	{
		return TAxisAlignedBox3(
			FVector3<RealType>(TNumericLimits<RealType>::Max(), TNumericLimits<RealType>::Max(), TNumericLimits<RealType>::Max()),
			FVector3<RealType>(-TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max()));
	}

	static TAxisAlignedBox3<RealType> Infinite()
	{
		return TAxisAlignedBox3(
			FVector3<RealType>(-TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max(), -TNumericLimits<RealType>::Max()),
			FVector3<RealType>(TNumericLimits<RealType>::Max(), TNumericLimits<RealType>::Max(), TNumericLimits<RealType>::Max()) );
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

	void Contain(const TAxisAlignedBox3<RealType>& Other)
	{
		Min.X = Min.X < Other.Min.X ? Min.X : Other.Min.X;
		Min.Y = Min.Y < Other.Min.Y ? Min.Y : Other.Min.Y;
		Min.Z = Min.Z < Other.Min.Z ? Min.Z : Other.Min.Z;
		Max.X = Max.X > Other.Max.X ? Max.X : Other.Max.X;
		Max.Y = Max.Y > Other.Max.Y ? Max.Y : Other.Max.Y;
		Max.Z = Max.Z > Other.Max.Z ? Max.Z : Other.Max.Z;
	}

	bool Contains(const FVector3<RealType>& V) const
	{
		return (Min.X <= V.X) && (Min.Y <= V.Y) && (Min.Z <= V.Z) && (Max.X >= V.X) && (Max.Y >= V.Y) && (Max.Z >= V.Z);
	}

	bool Contains(const TAxisAlignedBox3<RealType>& Box) const
	{
		return Contains(Box.Min) && Contains(Box.Max);
	}

	TAxisAlignedBox3<RealType> Intersect(const TAxisAlignedBox3<RealType>& Box) const
	{
		TAxisAlignedBox3<RealType> Intersection(
			FVector3<RealType>(TMathUtil<RealType>::Max(Min.X, Box.Min.X), TMathUtil<RealType>::Max(Min.Y, Box.Min.Y), TMathUtil<RealType>::Max(Min.Z, Box.Min.Z)),
			FVector3<RealType>(TMathUtil<RealType>::Min(Max.X, Box.Max.X), TMathUtil<RealType>::Min(Max.Y, Box.Max.Y), TMathUtil<RealType>::Min(Max.Z, Box.Max.Z)));
		if (Intersection.Height() <= 0 || Intersection.Width() <= 0 || Intersection.Depth() <= 0)
		{
			return TAxisAlignedBox3<RealType>::Empty();
		}
		else
		{
			return Intersection;
		}
	}

	bool Intersects(TAxisAlignedBox3 Box) const
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

	RealType DistanceSquared(const TAxisAlignedBox3<RealType>& Box)
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

	RealType MinDim() const
	{
		return TMathUtil<RealType>::Min(Width(), TMathUtil<RealType>::Min(Height(), Depth()));
	}

	FVector3<RealType> Diagonal() const
	{
		return FVector3<RealType>(Max.X - Min.X, Max.Y - Min.Y, Max.Z - Min.Z);
	}

	inline bool IsEmpty() const
	{
		return Max.X < Min.X || Max.Y < Min.Y || Max.Z < Min.Z;
	}

	void Expand(RealType Radius)
	{
		Max.X += Radius;
		Max.Y += Radius;
		Max.Z += Radius;
		Min.X -= Radius;
		Min.Y -= Radius;
		Min.Z -= Radius;
	}
};

template <typename RealType>
struct TAxisAlignedBox2
{
	FVector2<RealType> Min;
	FVector2<RealType> Max;

	TAxisAlignedBox2() : 
		TAxisAlignedBox2(Empty())
	{
	}

	TAxisAlignedBox2(const FVector2<RealType>& Min, const FVector2<RealType>& Max)
		: Min(Min), Max(Max)
	{
	}

	TAxisAlignedBox2(const TAxisAlignedBox2& OtherBox) = default;

	template<typename OtherRealType>
	explicit TAxisAlignedBox2(const TAxisAlignedBox2<OtherRealType>& OtherBox)
	{
		this->Min = FVector2<RealType>(OtherBox.Min);
		this->Max = FVector2<RealType>(OtherBox.Max);
	}

	TAxisAlignedBox2(RealType SquareSize)
		: Min((RealType)0, (RealType)0), Max(SquareSize, SquareSize)
	{
	}
	TAxisAlignedBox2(RealType Width, RealType Height)
		: Min((RealType)0, (RealType)0), Max(Width, Height)
	{
	}

	TAxisAlignedBox2(const TArray<FVector2<RealType>>& Pts)
	{
		*this = Empty();
		Contain(Pts);
	}

	TAxisAlignedBox2(const FVector2<RealType>& Center, RealType HalfWidth)
	{
		this->Min = FVector2<RealType>(Center.X - HalfWidth, Center.Y - HalfWidth);
		this->Max = FVector2<RealType>(Center.X + HalfWidth, Center.Y + HalfWidth);
	}

	explicit operator FBox2D() const
	{
		FVector2D MinV((float)Min.X, (float)Min.Y);
		FVector2D MaxV((float)Max.X, (float)Max.Y);
		return FBox2D(MinV, MaxV);
	}
	TAxisAlignedBox2(const FBox2D& Box)
	{
		Min = Box.Min;
		Max = Box.Max;
	}

	static TAxisAlignedBox2<RealType> Empty()
	{
		return TAxisAlignedBox2(
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

	/**
	 * Corners are ordered to follow the perimeter of the bounding rectangle, starting from the (Min.X, Min.Y) corner and ending at (Min.X, Max.Y)
	 * @param Index which corner to return, must be in range [0,3]
	 * @return Corner of the bounding rectangle
	 */
	FVector2<RealType> GetCorner(int Index) const
	{
		check(Index >= 0 && Index <= 3);
		RealType X = ((Index % 3) == 0) ? (Min.X) : (Max.X);
		RealType Y = ((Index & 2) == 0) ? (Min.Y) : (Max.Y);
		return FVector2<RealType>(X, Y);
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

	inline void Contain(const TAxisAlignedBox2<RealType>& Other)
	{
		Min.X = Min.X < Other.Min.X ? Min.X : Other.Min.X;
		Min.Y = Min.Y < Other.Min.Y ? Min.Y : Other.Min.Y;
		Max.X = Max.X > Other.Max.X ? Max.X : Other.Max.X;
		Max.Y = Max.Y > Other.Max.Y ? Max.Y : Other.Max.Y;
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

	bool Contains(const TAxisAlignedBox2<RealType>& Box) const
	{
		return Contains(Box.Min) && Contains(Box.Max);
	}

	bool Intersects(const TAxisAlignedBox2<RealType>& Box) const
	{
		return !((Box.Max.X < Min.X) || (Box.Min.X > Max.X) || (Box.Max.Y < Min.Y) || (Box.Min.Y > Max.Y));
	}

	TAxisAlignedBox2<RealType> Intersect(const TAxisAlignedBox2<RealType> &Box) const
	{
		TAxisAlignedBox2<RealType> Intersection(
			FVector2<RealType>(TMathUtil<RealType>::Max(Min.X, Box.Min.X), TMathUtil<RealType>::Max(Min.Y, Box.Min.Y)),
			FVector2<RealType>(TMathUtil<RealType>::Min(Max.X, Box.Max.X), TMathUtil<RealType>::Min(Max.Y, Box.Max.Y)));
		if (Intersection.Height() <= 0 || Intersection.Width() <= 0)
		{
			return TAxisAlignedBox2<RealType>::Empty();
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

	inline bool IsEmpty() const
	{
		return Max.X < Min.X || Max.Y < Min.Y;
	}

	void Expand(RealType Radius)
	{
		Max.X += Radius;
		Max.Y += Radius;
		Min.X -= Radius;
		Min.Y -= Radius;
	}
};

typedef TAxisAlignedBox2<float> FAxisAlignedBox2f;
typedef TAxisAlignedBox2<double> FAxisAlignedBox2d;
typedef TAxisAlignedBox2<int> FAxisAlignedBox2i;
typedef TAxisAlignedBox3<float> FAxisAlignedBox3f;
typedef TAxisAlignedBox3<double> FAxisAlignedBox3d;
typedef TAxisAlignedBox3<int> FAxisAlignedBox3i;
