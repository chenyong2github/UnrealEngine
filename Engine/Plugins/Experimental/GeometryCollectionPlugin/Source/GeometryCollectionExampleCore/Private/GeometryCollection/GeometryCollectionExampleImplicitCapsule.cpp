// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleImplicitCapsule.h"

#include "Chaos/Capsule.h"

namespace GeometryCollectionExample
{
	template <class T>
	void RunTestComputeSamplePoints(ExampleResponse& R, const Chaos::TCapsule<T> &Capsule)
	{
		const Chaos::TCapsule<T> OACapsule = Chaos::TCapsule<T>::NewFromOriginAndAxis(Capsule.GetOrigin(), Capsule.GetAxis(), Capsule.GetHeight(), Capsule.GetRadius());
		R.ExpectTrue((Capsule.GetOrigin() - OACapsule.GetOrigin()).Size() < KINDA_SMALL_NUMBER, FString("Capsule != OACapsule, origin."));
		R.ExpectTrue((Capsule.GetInsertion() - OACapsule.GetInsertion()).Size() < KINDA_SMALL_NUMBER, FString("Capsule != OACapsule, insertion."));
		R.ExpectTrue((Capsule.GetAxis() - OACapsule.GetAxis()).Size() < KINDA_SMALL_NUMBER, FString("Capsule != OACapsule, axis."));
		R.ExpectTrue(Capsule.GetHeight() - OACapsule.GetHeight() < KINDA_SMALL_NUMBER, FString("Capsule != OACapsule, height."));
		R.ExpectTrue(Capsule.GetRadius() - OACapsule.GetRadius() < KINDA_SMALL_NUMBER, FString("Capsule != OACapsule, radius."));

		Chaos::TVector<T, 3> Point;
		T Phi;
		R.ExpectTrue(Capsule.GetType() == Chaos::ImplicitObjectType::Capsule, FString("Implicit object type is not 'capsule'."));

		Point = Capsule.GetAxis();
		R.ExpectTrue(FMath::Abs(Point.Size() - 1.0) < KINDA_SMALL_NUMBER, FString("Capsule axis is not unit length."));
		
		Point = Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2);
		R.ExpectTrue(((Point - Capsule.GetInsertion()).Size() < KINDA_SMALL_NUMBER), FString("Capsule is broken."));

		Point = Capsule.GetInsertion();// Capsule.GetOrigin() + Capsule.GetAxis() * Capsule.GetHeight();
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(Phi <= KINDA_SMALL_NUMBER, FString("Capsule failed phi surface (insertion) sanity test."));

		Point = Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2) * 0.25;
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(Phi <= (T)0.0, FString("Capsule failed phi depth (1/4 origin) sanity test."));

		Point = Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2) * 0.75;
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(Phi <= (T)0.0, FString("Capsule failed phi depth (3/4 origin) sanity test."));

		R.ExpectTrue((Capsule.GetCenter() - Chaos::TVector<T,3>(Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2) * 0.5)).Size() <= KINDA_SMALL_NUMBER, FString("Capsule center is off mid axis."));

		Point = Capsule.GetCenter();
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(Phi < (T)0.0, FString("Capsule failed phi depth sanity test."));
		
		Point = Capsule.GetOrigin();
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Capsule failed phi surface (origin) sanity test."));

		Point = Capsule.GetInsertion();
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Capsule failed phi surface (origin+axis*height) sanity test."));

		Point = Capsule.GetOrigin() + Capsule.GetAxis() * Capsule.GetRadius() + Capsule.GetAxis().GetOrthogonalVector().GetSafeNormal() * Capsule.GetRadius();
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Capsule failed phi surface (origin+orthogonalAxis*radius) sanity test."));

		Point = Capsule.GetCenter() + Capsule.GetAxis().GetOrthogonalVector().GetSafeNormal() * Capsule.GetRadius();
		Phi = Capsule.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Capsule failed phi surface (center+orthogonalAxis*radius) sanity test."));

		TArray<Chaos::TVector<T, 3>> Points = Capsule.ComputeSamplePoints(100);
		check(Points.Num() == 100);
		Point[0] = TNumericLimits<T>::Max();
		T MinPhi = TNumericLimits<T>::Max();
		T MaxPhi = -TNumericLimits<T>::Max();
		for (int32 i=0; i < Points.Num(); i++)
		{
			const Chaos::TVector<T, 3> &Pt = Points[i];

			Phi = Capsule.SignedDistance(Pt);
			MinPhi = FMath::Min(Phi, MinPhi);
			MaxPhi = FMath::Max(Phi, MaxPhi);
			
			const bool Differs = Pt != Point;
			check(Differs);
			R.ExpectTrue(Differs, FString("Produced a redundant value."));
			Point = Pt;
		}

		const bool OnSurface = FMath::Abs(MinPhi) <= KINDA_SMALL_NUMBER && FMath::Abs(MaxPhi) <= KINDA_SMALL_NUMBER;
		check(OnSurface);
		R.ExpectTrue(OnSurface, FString("Produced a point not on the surface of the capsule."));
	}

	template <class T>
	void TestComputeSamplePoints_Capsule(ExampleResponse& R)
	{
		//
		// Height == 1
		//

		// At the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)1.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// At the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)10.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// At the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)0.1);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)1.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)10.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)0.1);
			RunTestComputeSamplePoints(R, Capsule);
		}

		//
		// Height > 1
		//

		// At the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)1.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// At the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)10.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// At the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)0.1);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)1.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)10.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)0.1);
			RunTestComputeSamplePoints(R, Capsule);
		}

		// 
		// Off axis
		//

		// At the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)1.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// At the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)10.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// At the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)0.1);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)1.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)10.0);
			RunTestComputeSamplePoints(R, Capsule);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)0.1);
			RunTestComputeSamplePoints(R, Capsule);
		}
	}

	template <class T>
	bool TestImplicitCapsule(ExampleResponse&& R)
	{
		TestComputeSamplePoints_Capsule<T>(R);
		return !R.HasError();
	}
	template bool TestImplicitCapsule<float>(ExampleResponse&& R);
} // namespace GeometryCollectionExample