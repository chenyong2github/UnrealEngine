// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp DistPoint3Triangle3

#pragma once

#include "VectorTypes.h"
#include "TriangleTypes.h"


/**
 * Compute unsigned distance between 3D point and 3D triangle
 */
template <typename Real>
class TDistPoint3Triangle3
{
public:
	// Input
	FVector3<Real> Point;
	TTriangle3<Real> Triangle;

	// Results
	FVector3<Real> TriangleBaryCoords;
	FVector3<Real> ClosestTrianglePoint;  // do we need this just use Triangle.BarycentricPoint


	TDistPoint3Triangle3(const FVector3<Real>& PointIn, const TTriangle3<Real>& TriangleIn)
	{
		Point = PointIn;
		Triangle = TriangleIn;
	}

	Real Get() 
	{
		return (Real)sqrt(ComputeResult());
	}
	Real GetSquared()
	{
		return ComputeResult();
	}

	Real ComputeResult()
	{
		FVector3<Real> diff = Triangle.V[0] - Point;
		FVector3<Real> edge0 = Triangle.V[1] - Triangle.V[0];
		FVector3<Real> edge1 = Triangle.V[2] - Triangle.V[0];
		Real a00 = edge0.SquaredLength();
		Real a01 = edge0.Dot(edge1);
		Real a11 = edge1.SquaredLength();
		Real b0 = diff.Dot(edge0);
		Real b1 = diff.Dot(edge1);
		Real c = diff.SquaredLength();
		Real det = FMath::Abs(a00*a11 - a01 * a01);
		Real s = a01 * b1 - a11 * b0;
		Real t = a01 * b0 - a00 * b1;
		Real sqrDistance;

		if (s + t <= det)
		{
			if (s < (Real)0)
			{
				if (t < (Real)0)  // region 4
				{
					if (b0 < (Real)0)
					{
						t = (Real)0;
						if (-b0 >= a00)
						{
							s = (Real)1;
							sqrDistance = a00 + ((Real)2)*b0 + c;
						}
						else
						{
							s = -b0 / a00;
							sqrDistance = b0 * s + c;
						}
					}
					else
					{
						s = (Real)0;
						if (b1 >= (Real)0)
						{
							t = (Real)0;
							sqrDistance = c;
						}
						else if (-b1 >= a11)
						{
							t = (Real)1;
							sqrDistance = a11 + ((Real)2)*b1 + c;
						}
						else
						{
							t = -b1 / a11;
							sqrDistance = b1 * t + c;
						}
					}
				}
				else  // region 3
				{
					s = (Real)0;
					if (b1 >= (Real)0)
					{
						t = (Real)0;
						sqrDistance = c;
					}
					else if (-b1 >= a11)
					{
						t = (Real)1;
						sqrDistance = a11 + ((Real)2)*b1 + c;
					}
					else
					{
						t = -b1 / a11;
						sqrDistance = b1 * t + c;
					}
				}
			}
			else if (t < (Real)0)  // region 5
			{
				t = (Real)0;
				if (b0 >= (Real)0)
				{
					s = (Real)0;
					sqrDistance = c;
				}
				else if (-b0 >= a00)
				{
					s = (Real)1;
					sqrDistance = a00 + ((Real)2)*b0 + c;
				}
				else
				{
					s = -b0 / a00;
					sqrDistance = b0 * s + c;
				}
			}
			else  // region 0
			{
				// minimum at interior point
				Real invDet = ((Real)1) / det;
				s *= invDet;
				t *= invDet;
				sqrDistance = s * (a00*s + a01 * t + ((Real)2)*b0) + t * (a01*s + a11 * t + ((Real)2)*b1) + c;
			}
		}
		else
		{
			Real tmp0, tmp1, numer, denom;

			if (s < (Real)0)  // region 2
			{
				tmp0 = a01 + b0;
				tmp1 = a11 + b1;
				if (tmp1 > tmp0)
				{
					numer = tmp1 - tmp0;
					denom = a00 - ((Real)2)*a01 + a11;
					if (numer >= denom)
					{
						s = (Real)1;
						t = (Real)0;
						sqrDistance = a00 + ((Real)2)*b0 + c;
					}
					else
					{
						s = numer / denom;
						t = (Real)1 - s;
						sqrDistance = s * (a00*s + a01 * t + ((Real)2)*b0) + t * (a01*s + a11 * t + ((Real)2)*b1) + c;
					}
				}
				else
				{
					s = (Real)0;
					if (tmp1 <= (Real)0)
					{
						t = (Real)1;
						sqrDistance = a11 + ((Real)2)*b1 + c;
					}
					else if (b1 >= (Real)0)
					{
						t = (Real)0;
						sqrDistance = c;
					}
					else
					{
						t = -b1 / a11;
						sqrDistance = b1 * t + c;
					}
				}
			}
			else if (t < (Real)0)  // region 6
			{
				tmp0 = a01 + b1;
				tmp1 = a00 + b0;
				if (tmp1 > tmp0)
				{
					numer = tmp1 - tmp0;
					denom = a00 - ((Real)2)*a01 + a11;
					if (numer >= denom)
					{
						t = (Real)1;
						s = (Real)0;
						sqrDistance = a11 + ((Real)2)*b1 + c;
					}
					else
					{
						t = numer / denom;
						s = (Real)1 - t;
						sqrDistance = s * (a00*s + a01 * t + ((Real)2)*b0) + t * (a01*s + a11 * t + ((Real)2)*b1) + c;
					}
				}
				else
				{
					t = (Real)0;
					if (tmp1 <= (Real)0)
					{
						s = (Real)1;
						sqrDistance = a00 + ((Real)2)*b0 + c;
					}
					else if (b0 >= (Real)0)
					{
						s = (Real)0;
						sqrDistance = c;
					}
					else
					{
						s = -b0 / a00;
						sqrDistance = b0 * s + c;
					}
				}
			}
			else  // region 1
			{
				numer = a11 + b1 - a01 - b0;
				if (numer <= (Real)0)
				{
					s = (Real)0;
					t = (Real)1;
					sqrDistance = a11 + ((Real)2)*b1 + c;
				}
				else
				{
					denom = a00 - ((Real)2)*a01 + a11;
					if (numer >= denom)
					{
						s = (Real)1;
						t = (Real)0;
						sqrDistance = a00 + ((Real)2)*b0 + c;
					}
					else
					{
						s = numer / denom;
						t = (Real)1 - s;
						sqrDistance = s * (a00*s + a01 * t + ((Real)2)*b0) + t * (a01*s + a11 * t + ((Real)2)*b1) + c;
					}
				}
			}
		}

		// Account for numerical round-off error.
		if (sqrDistance < (Real)0)
		{
			sqrDistance = (Real)0;
		}

		ClosestTrianglePoint = Triangle.V[0] + s * edge0 + t * edge1;
		TriangleBaryCoords = FVector3<Real>((Real)1 - s - t, s, t);
		return sqrDistance;
	}

};

typedef TDistPoint3Triangle3<float> FDistPoint3Triangle3f;
typedef TDistPoint3Triangle3<double> FDistPoint3Triangle3d;

