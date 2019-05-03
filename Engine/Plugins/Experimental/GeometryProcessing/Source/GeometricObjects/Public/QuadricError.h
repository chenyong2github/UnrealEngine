// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"


/**
 * QuadricError represents a quadratic function that evaluates distance to plane.
 * Stores minimal 10-coefficient form, following http://mgarland.org/files/papers/qtheory.pdf
 * (symmetric matrix A, vector b, constant c)
 */
template<typename RealType>
struct TQuadricError 
{
	RealType Axx, Axy, Axz, Ayy, Ayz, Azz;
	RealType bx, by, bz;
	RealType c;

	inline static TQuadricError Zero() { return TQuadricError(); };

	TQuadricError()
	{
		Axx = Axy = Axz = Ayy = Ayz = Azz = bx = by = bz = c = 0;
	}

	/**
	 * Construct TQuadricError a plane with the given normal and a point on plane
	 */
	TQuadricError(const FVector3<RealType>& Normal, const FVector3<RealType>& Point) 
	{
		Axx = Normal.X * Normal.X;
		Axy = Normal.X * Normal.Y;
		Axz = Normal.X * Normal.Z;
		Ayy = Normal.Y * Normal.Y;
		Ayz = Normal.Y * Normal.Z;
		Azz = Normal.Z * Normal.Z;
		bx = by = bz = c = 0;
		FVector3<RealType> v = MultiplyA(Point);
		bx = -v.X; by = -v.Y; bz = -v.Z;
		c = Point.Dot(v);
	}

	/**
	 * Construct TQuadricError that is the sum of two other TQuadricErrors
	 */
	TQuadricError(const TQuadricError& a, const TQuadricError& b) 
	{
		Axx = a.Axx + b.Axx;
		Axy = a.Axy + b.Axy;
		Axz = a.Axz + b.Axz;
		Ayy = a.Ayy + b.Ayy;
		Ayz = a.Ayz + b.Ayz;
		Azz = a.Azz + b.Azz;
		bx = a.bx + b.bx;
		by = a.by + b.by;
		bz = a.bz + b.bz;
		c = a.c + b.c;
	}


	/**
	 * Add scalar multiple of a TQuadricError to this TQuadricError
	 */
	void Add(RealType w, const TQuadricError& b) 
	{
		Axx += w * b.Axx;
		Axy += w * b.Axy;
		Axz += w * b.Axz;
		Ayy += w * b.Ayy;
		Ayz += w * b.Ayz;
		Azz += w * b.Azz;
		bx += w * b.bx;
		by += w * b.by;
		bz += w * b.bz;
		c += w * b.c;
	}


	/**
	 * Evaluates p*A*p + 2*dot(p,b) + c
	 * @return 
	 */
	RealType Evaluate(const FVector3<RealType>& pt) const
	{
		RealType x = Axx * pt.X + Axy * pt.Y + Axz * pt.Z;
		RealType y = Axy * pt.X + Ayy * pt.Y + Ayz * pt.Z;
		RealType z = Axz * pt.X + Ayz * pt.Y + Azz * pt.Z;
		return (pt.X * x + pt.Y * y + pt.Z * z) +
			2.0 * (pt.X * bx + pt.Y * by + pt.Z * bz) + c;
	}

	/**
	 * 
	 */
	FVector3<RealType> MultiplyA(const FVector3<RealType>& pt) const
	{
		RealType x = Axx * pt.X + Axy * pt.Y + Axz * pt.Z;
		RealType y = Axy * pt.X + Ayy * pt.Y + Ayz * pt.Z;
		RealType z = Axz * pt.X + Ayz * pt.Y + Azz * pt.Z;
		return FVector3<RealType>(x, y, z);
	}



	bool OptimalPoint(FVector3<RealType>& OutResult, RealType minThresh = 1000.0*TMathUtil<RealType>::Epsilon ) const
	{
		RealType a11 = Azz * Ayy - Ayz * Ayz;
		RealType a12 = Axz * Ayz - Azz * Axy;
		RealType a13 = Axy * Ayz - Axz * Ayy;
		RealType a22 = Azz * Axx - Axz * Axz;
		RealType a23 = Axy * Axz - Axx * Ayz;
		RealType a33 = Axx * Ayy - Axy * Axy;
		RealType det = (Axx * a11) + (Axy * a12) + (Axz * a13);

		// [RMS] not sure what we should be using for this threshold...have seen
		//  det less than 10^-9 on "normal" meshes.
		if ( (RealType)fabs(det) > minThresh)
		{
			det = 1.0 / det;
			a11 *= det; a12 *= det; a13 *= det;
			a22 *= det; a23 *= det; a33 *= det;
			RealType x = a11 * bx + a12 * by + a13 * bz;
			RealType y = a12 * bx + a22 * by + a23 * bz;
			RealType z = a13 * bx + a23 * by + a33 * bz;
			OutResult = FVector3<RealType>(-x, -y, -z);
			return true;
		}
		else 
		{
			return false;
		}

	}

};


typedef TQuadricError<float> FQuadricErrorf;
typedef TQuadricError<double> FQuadricErrord;