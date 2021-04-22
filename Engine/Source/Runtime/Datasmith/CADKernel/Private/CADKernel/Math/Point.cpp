// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Math/Point.h"

#include "CADKernel/Utils/Util.h"
#include "CADKernel/Math/MatrixH.h"

using namespace CADKernel;

const FPoint FPoint::ZeroPoint(0.,0.,0.);
const FFPoint FFPoint::ZeroPoint(0.f, 0.f, 0.f);
const FPoint2D FPoint2D::ZeroPoint(0., 0.);
const FPointH FPointH::ZeroPoint(0., 0., 0., 1.);

double FPoint::SignedAngle(const FPoint & Other, const FPoint & Normal) const
{
	FPoint Vector1 = *this; 
	FPoint Vector2 = Other; 
	FPoint Vector3 = Normal; 

	Vector1.Normalize();
	Vector2.Normalize();
	Vector3.Normalize();

	double ScalarProduct = Vector1 * Vector2;

	if (ScalarProduct >= 1 - SMALL_NUMBER)
	{
		return 0.;
	}

	if (ScalarProduct <= -1 + SMALL_NUMBER)
	{
		return PI;
	}

	return MixedTripleProduct(Vector1, Vector2, Vector3) > 0 ? acos(ScalarProduct) : -acos(ScalarProduct);
}

double FPoint::ComputeCosinus(const FPoint& OtherVector) const
{
	FPoint ThisNormalized = *this;
	FPoint OtherNormalized = OtherVector;

	ThisNormalized.Normalize();
	OtherNormalized.Normalize();

	double Cosinus = ThisNormalized * OtherNormalized;

	return FMath::Max(-1.0, FMath::Min(Cosinus, 1.0));
}

double FPoint::ComputeSinus(const FPoint& OtherVector) const
{
	FPoint ThisNormalized = *this;
	FPoint OtherNormalized = OtherVector;

	ThisNormalized.Normalize();
	OtherNormalized.Normalize();

	FPoint SinusPoint = ThisNormalized ^ OtherNormalized;
	double Sinus = SinusPoint.Length();
	return FMath::Min(Sinus, 1.0);
}

double FPoint::ComputeAngle(const FPoint& OtherVector) const
{
	double CosAngle = ComputeCosinus(OtherVector);
	return acos(CosAngle);
}
