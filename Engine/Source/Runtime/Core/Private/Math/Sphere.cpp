// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Sphere.cpp: Implements the FSphere class.
=============================================================================*/

#include "Math/Sphere.h"
#include "Math/Box.h"
#include "Math/Transform.h"

// [ Ritter 1990, "An Efficient Bounding Sphere" ]
FSphere::FSphere( const FVector* Points, int32 Num )
{
	check( Num > 0 );

	// Min/max points of AABB
	int32 MinIndex[3] = { 0, 0, 0 };
	int32 MaxIndex[3] = { 0, 0, 0 };

	for( int i = 0; i < Num; i++ )
	{
		for( int k = 0; k < 3; k++ )
		{
			MinIndex[k] = Points[i][k] < Points[ MinIndex[k] ][k] ? i : MinIndex[k];
			MaxIndex[k] = Points[i][k] > Points[ MaxIndex[k] ][k] ? i : MaxIndex[k];
		}
	}

	float LargestDistSqr = 0.0f;
	int32 LargestAxis = 0;
	for( int k = 0; k < 3; k++ )
	{
		FVector PointMin = Points[ MinIndex[k] ];
		FVector PointMax = Points[ MaxIndex[k] ];

		float DistSqr = ( PointMax - PointMin ).SizeSquared();
		if( DistSqr > LargestDistSqr )
		{
			LargestDistSqr = DistSqr;
			LargestAxis = k;
		}
	}

	FVector PointMin = Points[ MinIndex[ LargestAxis ] ];
	FVector PointMax = Points[ MaxIndex[ LargestAxis ] ];

	Center = 0.5f * ( PointMin + PointMax );
	W = 0.5f * FMath::Sqrt( LargestDistSqr );

	// Adjust to fit all points
	for( int i = 0; i < Num; i++ )
	{
		float DistSqr = ( Points[i] - Center ).SizeSquared();

		if( DistSqr > W*W )
		{
			float Dist = FMath::Sqrt( DistSqr );
			float t = 0.5f + 0.5f * ( W / Dist );

			Center = FMath::LerpStable( Points[i], Center, t );
			W = 0.5f * ( W + Dist );
		}
	}
}

FSphere::FSphere( const FSphere* Spheres, int32 Num )
{
	check( Num > 0 );

	// Min/max points of AABB
	int32 MinIndex[3] = { 0, 0, 0 };
	int32 MaxIndex[3] = { 0, 0, 0 };

	for( int i = 0; i < Num; i++ )
	{
		for( int k = 0; k < 3; k++ )
		{
			MinIndex[k] = Spheres[i].Center[k] - Spheres[i].W < Spheres[ MinIndex[k] ].Center[k] - Spheres[ MinIndex[k] ].W ? i : MinIndex[k];
			MaxIndex[k] = Spheres[i].Center[k] + Spheres[i].W > Spheres[ MaxIndex[k] ].Center[k] + Spheres[ MaxIndex[k] ].W ? i : MaxIndex[k];
		}
	}

	float LargestDist = 0.0f;
	int32 LargestAxis = 0;
	for( int k = 0; k < 3; k++ )
	{
		FSphere SphereMin = Spheres[ MinIndex[k] ];
		FSphere SphereMax = Spheres[ MaxIndex[k] ];

		float Dist = ( SphereMax.Center - SphereMin.Center ).Size() + SphereMin.W + SphereMax.W;
		if( Dist > LargestDist )
		{
			LargestDist = Dist;
			LargestAxis = k;
		}
	}

	*this  = Spheres[ MinIndex[ LargestAxis ] ];
	*this += Spheres[ MaxIndex[ LargestAxis ] ];

	// Adjust to fit all spheres
	for( int i = 0; i < Num; i++ )
	{
		*this += Spheres[i];
	}
}

FSphere FSphere::TransformBy(const FMatrix& M) const
{
	FSphere	Result;

	FVector4 TransformedCenter = M.TransformPosition(this->Center);
	Result.Center = FVector(TransformedCenter.X, TransformedCenter.Y, TransformedCenter.Z);

	const FVector XAxis(M.M[0][0], M.M[0][1], M.M[0][2]);
	const FVector YAxis(M.M[1][0], M.M[1][1], M.M[1][2]);
	const FVector ZAxis(M.M[2][0], M.M[2][1], M.M[2][2]);

	Result.W = FMath::Sqrt(FMath::Max(XAxis | XAxis, FMath::Max(YAxis | YAxis, ZAxis | ZAxis))) * W;

	return Result;
}


FSphere FSphere::TransformBy(const FTransform& M) const
{
	FSphere	Result;

	Result.Center = M.TransformPosition(this->Center);
	Result.W = M.GetMaximumAxisScale() * W;

	return Result;
}

float FSphere::GetVolume() const
{
	return (4.f / 3.f) * PI * (W * W * W);
}

FSphere& FSphere::operator+=(const FSphere &Other)
{
	if (W == 0.f)
	{
		*this = Other;
		return *this;
	}
	
	FVector ToOther = Other.Center - Center;
	float DistSqr = ToOther.SizeSquared();

	if( FMath::Square( W - Other.W ) + KINDA_SMALL_NUMBER >= DistSqr )
	{
		// Pick the smaller
		if( W < Other.W )
		{
			*this = Other;
		}
	}
	else
	{
		float Dist = FMath::Sqrt( DistSqr );

		FSphere NewSphere;
		NewSphere.W = ( Dist + Other.W + W ) * 0.5f;
		NewSphere.Center = Center;

		if( Dist > SMALL_NUMBER )
		{
			NewSphere.Center += ToOther * ( ( NewSphere.W - W ) / Dist );
		}

		// make sure both are inside afterwards
		checkSlow (Other.IsInside(NewSphere, 1.f));
		checkSlow (IsInside(NewSphere, 1.f));

		*this = NewSphere;
	}

	return *this;
}