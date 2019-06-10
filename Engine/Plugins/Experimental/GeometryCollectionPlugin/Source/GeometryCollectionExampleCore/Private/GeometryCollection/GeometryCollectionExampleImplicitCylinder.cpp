// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleImplicitCylinder.h"

#include "Chaos/Cylinder.h"

namespace GeometryCollectionExample
{
	template <class T>
	void RunTestComputeSamplePoints(ExampleResponse& R, const Chaos::TCylinder<T> &Cylinder)
	{
		Chaos::TVector<T, 3> Point;
		T Phi;
		R.ExpectTrue(Cylinder.GetType() == Chaos::ImplicitObjectType::Cylinder, FString("Implicit object type is not 'cylinder'."));

		Point = Cylinder.GetAxis();
		R.ExpectTrue(FMath::Abs(Point.Size() - 1.0) < KINDA_SMALL_NUMBER, FString("Cylinder axis is not unit length."));
		
		Point = Cylinder.GetOrigin() + Cylinder.GetAxis() * Cylinder.GetHeight();
		R.ExpectTrue(((Point - Cylinder.GetInsertion()).Size() < KINDA_SMALL_NUMBER), FString("Cylinder is broken."));

		Point = Cylinder.GetInsertion();// Cylinder.GetOrigin() + Cylinder.GetAxis() * Cylinder.GetHeight();
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(Phi <= KINDA_SMALL_NUMBER, FString("Cylinder failed phi surface (insertion) sanity test."));

		Point = Cylinder.GetOrigin() + Cylinder.GetAxis() * Cylinder.GetHeight() * 0.25;
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(Phi <= (T)0.0, FString("Cylinder failed phi depth (1/4 origin) sanity test."));

		Point = Cylinder.GetOrigin() + Cylinder.GetAxis() * Cylinder.GetHeight() * 0.75;
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(Phi <= (T)0.0, FString("Cylinder failed phi depth (3/4 origin) sanity test."));

		R.ExpectTrue((Cylinder.GetCenter() - Chaos::TVector<T,3>(Cylinder.GetOrigin() + Cylinder.GetAxis() * Cylinder.GetHeight() * 0.5)).Size() <= KINDA_SMALL_NUMBER, FString("Cylinder center is off mid axis."));

		Point = Cylinder.GetCenter();
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(Phi < (T)0.0, FString("Cylinder failed phi depth sanity test."));
		
		Point = Cylinder.GetOrigin();
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Cylinder failed phi surface (origin) sanity test."));

		Point = Cylinder.GetOrigin() + Cylinder.GetAxis() * Cylinder.GetHeight();
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Cylinder failed phi surface (origin+axis*height) sanity test."));

		Point = Cylinder.GetOrigin() + Cylinder.GetAxis().GetOrthogonalVector().GetSafeNormal() * Cylinder.GetRadius();
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Cylinder failed phi surface (origin+orthogonalAxis*radius) sanity test."));

		Point = Cylinder.GetCenter() + Cylinder.GetAxis().GetOrthogonalVector().GetSafeNormal() * Cylinder.GetRadius();
		Phi = Cylinder.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Cylinder failed phi surface (center+orthogonalAxis*radius) sanity test."));

		TArray<Chaos::TVector<T, 3>> Points = Cylinder.ComputeSamplePoints(100);
		check(Points.Num() == 100);
		Point[0] = TNumericLimits<T>::Max();
		T MinPhi = TNumericLimits<T>::Max();
		T MaxPhi = -TNumericLimits<T>::Max();
		for (int32 i=0; i < Points.Num(); i++)
		{
			const Chaos::TVector<T, 3> &Pt = Points[i];

			Phi = Cylinder.SignedDistance(Pt);
			MinPhi = FMath::Min(Phi, MinPhi);
			MaxPhi = FMath::Max(Phi, MaxPhi);
			
			const bool Differs = Pt != Point;
			check(Differs);
			R.ExpectTrue(Differs, FString("Produced a redundant value."));
			Point = Pt;
		}

		const bool OnSurface = FMath::Abs(MinPhi) <= KINDA_SMALL_NUMBER && FMath::Abs(MaxPhi) <= KINDA_SMALL_NUMBER;
		check(OnSurface);
		R.ExpectTrue(OnSurface, FString("Produced a point not on the surface of the cylinder."));
	}

	template <class T>
	void TestComputeSamplePoints_Cylinder(ExampleResponse& R)
	{
		//
		// Height == 1
		//

		// At the origin with radius 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)1.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// At the origin with radius > 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)10.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// At the origin with radius < 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)0.1);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)1.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)10.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)0.1);
			RunTestComputeSamplePoints(R, Cylinder);
		}

		//
		// Height > 1
		//

		// At the origin with radius 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)1.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// At the origin with radius > 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)10.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// At the origin with radius < 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)0.1);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)1.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)10.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)0.1);
			RunTestComputeSamplePoints(R, Cylinder);
		}

		// 
		// Off axis
		//

		// At the origin with radius 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)1.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// At the origin with radius > 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)10.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// At the origin with radius < 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)0.1);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)1.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)10.0);
			RunTestComputeSamplePoints(R, Cylinder);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCylinder<T> Cylinder(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)0.1);
			RunTestComputeSamplePoints(R, Cylinder);
		}
	}

	template <class T>
	bool TestImplicitCylinder(ExampleResponse&& R)
	{
		TestComputeSamplePoints_Cylinder<T>(R);
		return !R.HasError();
	}
	template bool TestImplicitCylinder<float>(ExampleResponse&& R);
} // namespace GeometryCollectionExample