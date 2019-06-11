// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleImplicitSphere.h"

#include "Chaos/Sphere.h"

namespace GeometryCollectionExample
{
	template <class T, int d>
	void RunTestComputeSamplePoints(ExampleResponse& R, const Chaos::TSphere<T, d> &Sphere)
	{
		R.ExpectTrue(Sphere.GetType() == Chaos::ImplicitObjectType::Sphere, FString("Implicit object type is not 'sphere'."));

		Chaos::TVector<T, d> Point = Sphere.Center();
		T Phi = Sphere.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi + Sphere.Radius()) <= SMALL_NUMBER, FString("Sphere failed phi depth sanity test."));
		
		Point[0] += Sphere.Radius();
		Phi = Sphere.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi) <= KINDA_SMALL_NUMBER, FString("Sphere failed phi surface sanity test."));

		TArray<Chaos::TVector<T, d>> Points = Sphere.ComputeSamplePoints(100);
		check(Points.Num() == 100);
		Point[0] = TNumericLimits<T>::Max();
		for (const Chaos::TVector<T, d> &Pt : Points)
		{
			Phi = Sphere.SignedDistance(Pt);
			const bool OnSurface = FMath::Abs(Phi) <= KINDA_SMALL_NUMBER;
			check(OnSurface);
			R.ExpectTrue(OnSurface, FString("Produced a point not on the surface of the sphere."));
			const bool Differs = Pt != Point;
			check(Differs);
			R.ExpectTrue(Differs, FString("Produced a redundant value."));
			Point = Pt;
		}
	}

	template <class T>
	void RunTestComputeSemispherePoints(ExampleResponse& R, const Chaos::TSphere<T, 3> &Sphere)
	{
		TArray<Chaos::TVector<T, 3>> Points;
		Chaos::TSphereSpecializeSamplingHelper<T, 3>::ComputeBottomHalfSemiSphere(Points, Sphere, 100);
		for (Chaos::TVector<T, 3>& Pt : Points)
		{
			T Phi = Sphere.SignedDistance(Pt);
			const bool OnSurface = FMath::Abs(Phi) <= KINDA_SMALL_NUMBER;
			check(OnSurface);
			R.ExpectTrue(OnSurface, FString("Produced a point not on the surface of the sphere."));

			const bool BelowCenter = Pt[2] < Sphere.Center()[2] + KINDA_SMALL_NUMBER;
			check(BelowCenter);
			R.ExpectTrue(BelowCenter, FString("Bottom semisphere produced a point above midline."));
		}

		Points.Reset();
		Chaos::TSphereSpecializeSamplingHelper<T, 3>::ComputeTopHalfSemiSphere(Points, Sphere, 100);
		for (Chaos::TVector<T, 3>& Pt : Points)
		{
			T Phi = Sphere.SignedDistance(Pt);
			const bool OnSurface = FMath::Abs(Phi) <= KINDA_SMALL_NUMBER;
			check(OnSurface);
			R.ExpectTrue(OnSurface, FString("Produced a point not on the surface of the sphere."));

			const bool BelowCenter = Pt[2] > Sphere.Center()[2] - KINDA_SMALL_NUMBER;
			check(BelowCenter);
			R.ExpectTrue(BelowCenter, FString("Top semisphere produced a point above midline."));
		}
	}

	template <class T>
	void TestComputeSamplePoints_SemiSphere(ExampleResponse& R)
	{
		// At the origin with radius 1
		{
			Chaos::TSphere<T, 3> Sphere(Chaos::TVector<T, 3>((T)0.0), (T)1.0);
			RunTestComputeSemispherePoints(R, Sphere);
		}	
	}

	template <class T, int d>
	void TestComputeSamplePoints_Sphere(ExampleResponse& R)
	{
		// At the origin with radius 1
		{
			Chaos::TSphere<T, d> Sphere(Chaos::TVector<T, d>((T)0.0), (T)1.0);
			RunTestComputeSamplePoints(R, Sphere);
		}
		// At the origin with radius > 1
		{
			Chaos::TSphere<T, d> Sphere(Chaos::TVector<T, d>(0.0), (T)10.0);
			RunTestComputeSamplePoints(R, Sphere);
		}
		// At the origin with radius < 1
		{
			Chaos::TSphere<T, d> Sphere(Chaos::TVector<T, d>(0.0), (T).1);
			RunTestComputeSamplePoints(R, Sphere);
		}
		// Off the origin with radius 1
		{
			Chaos::TSphere<T, d> Sphere(Chaos::TVector<T, d>(10.0), (T)1.0);
			RunTestComputeSamplePoints(R, Sphere);
		}
		// Off the origin with radius > 1
		{
			Chaos::TSphere<T, d> Sphere(Chaos::TVector<T, d>(10.0), (T)10.0);
			RunTestComputeSamplePoints(R, Sphere);
		}
		// Off the origin with radius < 1
		{
			Chaos::TSphere<T, d> Sphere(Chaos::TVector<T, d>(10.0), (T).1);
			RunTestComputeSamplePoints(R, Sphere);
		}
	}

	template<class T>
	bool TestImplicitSphere(ExampleResponse&& R)
	{
		//TestComputeSamplePoints<T, 2>(R);
		TestComputeSamplePoints_Sphere<T, 3>(R);
		TestComputeSamplePoints_SemiSphere<T>(R);
		return !R.HasError();
	}
	template bool TestImplicitSphere<float>(ExampleResponse&& R);
} // namespace GeometryCollectionExample