// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleImplicitCapsule.h"

#include "Chaos/Capsule.h"

namespace GeometryCollectionExample
{
	template <class T>
	void RunTestComputeSamplePoints(const Chaos::TCapsule<T> &Capsule)
	{
		const Chaos::TCapsule<T> OACapsule = Chaos::TCapsule<T>::NewFromOriginAndAxis(Capsule.GetOrigin(), Capsule.GetAxis(), Capsule.GetHeight(), Capsule.GetRadius());
		EXPECT_NEAR((Capsule.GetOrigin() - OACapsule.GetOrigin()).Size(), 0, KINDA_SMALL_NUMBER) << *FString("Capsule != OACapsule, origin.");
		EXPECT_NEAR((Capsule.GetInsertion() - OACapsule.GetInsertion()).Size(), 0, KINDA_SMALL_NUMBER) << *FString("Capsule != OACapsule, insertion.");
		EXPECT_NEAR((Capsule.GetAxis() - OACapsule.GetAxis()).Size(), 0, KINDA_SMALL_NUMBER) << *FString("Capsule != OACapsule, axis.");
		EXPECT_NEAR(Capsule.GetHeight() - OACapsule.GetHeight(), 0, KINDA_SMALL_NUMBER) << *FString("Capsule != OACapsule, height.");
		EXPECT_NEAR(Capsule.GetRadius() - OACapsule.GetRadius(), 0, KINDA_SMALL_NUMBER) << *FString("Capsule != OACapsule, radius.");

		Chaos::TVector<T, 3> Point;
		T Phi;
		EXPECT_EQ(Capsule.GetType(), Chaos::ImplicitObjectType::Capsule) << *FString("Implicit object type is not 'capsule'.");

		Point = Capsule.GetAxis();
		EXPECT_LT(FMath::Abs(Point.Size() - 1.0), KINDA_SMALL_NUMBER) << *FString("Capsule axis is not unit length.");
		
		Point = Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2);
		EXPECT_LT((Point - Capsule.GetInsertion()).Size(), KINDA_SMALL_NUMBER) << *FString("Capsule is broken.");

		Point = Capsule.GetInsertion();// Capsule.GetOrigin() + Capsule.GetAxis() * Capsule.GetHeight();
		Phi = Capsule.SignedDistance(Point);
		EXPECT_NEAR(Phi, 0, KINDA_SMALL_NUMBER) << *FString("Capsule failed phi surface (insertion) sanity test.");

		Point = Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2) * 0.25;
		Phi = Capsule.SignedDistance(Point);
		EXPECT_LE(Phi, (T)0.0) << *FString("Capsule failed phi depth (1/4 origin) sanity test.");

		Point = Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2) * 0.75;
		Phi = Capsule.SignedDistance(Point);
		EXPECT_LE(Phi, (T)0.0) << *FString("Capsule failed phi depth (3/4 origin) sanity test.");

		EXPECT_NEAR((Capsule.GetCenter() - Chaos::TVector<T,3>(Capsule.GetOrigin() + Capsule.GetAxis() * (Capsule.GetHeight() + Capsule.GetRadius() * 2) * 0.5)).Size(), 0, KINDA_SMALL_NUMBER) << *FString("Capsule center is off mid axis.");

		Point = Capsule.GetCenter();
		Phi = Capsule.SignedDistance(Point);
		EXPECT_LT(Phi, (T)0.0) << *FString("Capsule failed phi depth sanity test.");
		
		Point = Capsule.GetOrigin();
		Phi = Capsule.SignedDistance(Point);
		EXPECT_NEAR(FMath::Abs(Phi), 0, KINDA_SMALL_NUMBER) << *FString("Capsule failed phi surface (origin) sanity test.");

		Point = Capsule.GetInsertion();
		Phi = Capsule.SignedDistance(Point);
		EXPECT_NEAR(FMath::Abs(Phi), 0, KINDA_SMALL_NUMBER) << *FString("Capsule failed phi surface (origin+axis*height) sanity test.");

		Point = Capsule.GetOrigin() + Capsule.GetAxis() * Capsule.GetRadius() + Capsule.GetAxis().GetOrthogonalVector().GetSafeNormal() * Capsule.GetRadius();
		Phi = Capsule.SignedDistance(Point);
		EXPECT_NEAR(FMath::Abs(Phi), 0, KINDA_SMALL_NUMBER) << *FString("Capsule failed phi surface (origin+orthogonalAxis*radius) sanity test.");

		Point = Capsule.GetCenter() + Capsule.GetAxis().GetOrthogonalVector().GetSafeNormal() * Capsule.GetRadius();
		Phi = Capsule.SignedDistance(Point);
		EXPECT_NEAR(FMath::Abs(Phi), 0, KINDA_SMALL_NUMBER) << *FString("Capsule failed phi surface (center+orthogonalAxis*radius) sanity test.");

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
			EXPECT_TRUE(Differs) << *FString("Produced a redundant value.");
			Point = Pt;
		}

		const bool OnSurface = FMath::Abs(MinPhi) <= KINDA_SMALL_NUMBER && FMath::Abs(MaxPhi) <= KINDA_SMALL_NUMBER;
		check(OnSurface);
		EXPECT_TRUE(OnSurface) << *FString("Produced a point not on the surface of the capsule.");
	}

	template <class T>
	void TestComputeSamplePoints_Capsule()
	{
		//
		// Height == 1
		//

		// At the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)1.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// At the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)10.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// At the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,1), (T)0.1);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)1.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)10.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,11), (T)0.1);
			RunTestComputeSamplePoints(Capsule);
		}

		//
		// Height > 1
		//

		// At the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)1.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// At the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)10.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// At the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(0,0,10), (T)0.1);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)1.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)10.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(10,10,21), (T)0.1);
			RunTestComputeSamplePoints(Capsule);
		}

		// 
		// Off axis
		//

		// At the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)1.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// At the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)10.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// At the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(0,0,0), Chaos::TVector<T, 3>(1,1,1), (T)0.1);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)1.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius > 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)10.0);
			RunTestComputeSamplePoints(Capsule);
		}
		// Off the origin with radius < 1
		{
			Chaos::TCapsule<T> Capsule(Chaos::TVector<T, 3>(10,10,10), Chaos::TVector<T, 3>(11,11,11), (T)0.1);
			RunTestComputeSamplePoints(Capsule);
		}
	}

	template <class T>
	void TestImplicitCapsule()
	{
		TestComputeSamplePoints_Capsule<T>();
	}
	template void TestImplicitCapsule<float>();
} // namespace GeometryCollectionExample