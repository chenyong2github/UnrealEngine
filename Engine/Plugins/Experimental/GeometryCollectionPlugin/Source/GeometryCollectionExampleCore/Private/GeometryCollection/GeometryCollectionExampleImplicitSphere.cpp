// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleImplicitSphere.h"

#include "Chaos/Pair.h"
#include "Chaos/Sphere.h"

#include <string>

namespace GeometryCollectionExample
{
	//==========================================================================
	// FindClosestIntersection() tests
	//==========================================================================

	typedef Chaos::Pair<Chaos::TVector<float, 3>, bool> IntersectionResult;

	template <class T>
	void RunTestFindClosestIntersectionHelper(
		ExampleResponse& R, 
		const FString TestName,
		const Chaos::TVector<T, 3>& Center, 
		const T Radius,
		const T Thickness,
		const Chaos::TVector<T, 3>& StartPt,
		const Chaos::TVector<T, 3>& EndPt,
		const IntersectionResult ExpectedRes)
	{
		Chaos::TSphere<T, 3> Sphere(Center, Radius);
		IntersectionResult Res = Sphere.FindClosestIntersection(StartPt, EndPt, Thickness);
		R.ExpectTrue(
			(!Res.Second && !ExpectedRes.Second) ||
			(Res.Second && ExpectedRes.Second && (Res.First - ExpectedRes.First).Size() < SMALL_NUMBER),
			FString::Printf(
				TEXT(
					"%s - Sphere((%g, %g, %g), %g).FindClosestIntersection("
						"StartPt(%g, %g, %g), "
						"EndPt(%g, %g, %g), "
						"Thickness:%g) = "
					"RESULT: %d, Point: (%g, %g, %g), "
					"EXPECTED: %d (%g, %g, %g)."),
				*TestName,
				Center[0], Center[1], Center[2],
				Radius,

				StartPt[0], StartPt[1], StartPt[2],
				EndPt[0], EndPt[1], EndPt[2],
				Thickness,
				
				Res.Second ? 1 : 0,
				Res.First[0], Res.First[1], Res.First[2],
				
				ExpectedRes.Second ? 1 : 0,
				ExpectedRes.First[0], ExpectedRes.First[1], ExpectedRes.First[2]
			));
	}

	template <class T>
	void RunTestFindClosestIntersection_Tangent(
		ExampleResponse& R,
		const Chaos::TVector<T, 3>& Center,
		const T Radius,
		const T Thickness)
	{
		Chaos::TVector<T, 3> StartPt;
		Chaos::TVector<T, 3> EndPt;

		// Start & End points coincident, lying on the surface of the sphere w/zero thickness
		// Fails with no collision:
		// "RunTestFindClosestIntersection_Tangent1 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 1), EndPt(0, 0, 1), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		StartPt = EndPt = Center + Chaos::TVector<T,3>(0, 0, Radius);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Tangent1", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		StartPt = Center + Chaos::TVector<T, 3>(Radius, 0, Radius+Thickness);
		EndPt = Center + Chaos::TVector<T, 3>(-Radius, 0, Radius+Thickness);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Tangent2", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));
		
		// Start & End points coincident, lying on the surface of the sphere w/thickness
		// Fails with incorrect collision point:
		// "RunTestFindClosestIntersection_Tangent3 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 1.1), EndPt(0, 0, 1.1), Thickness:0.1) = RESULT: 1, Point: (0, 0, 1), EXPECTED: 1 (0, 0, 1.1)."	FString
		// "RunTestFindClosestIntersection_Tangent3 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 1.5), EndPt(0, 0, 1.5), Thickness:0.5) = RESULT: 1, Point: (0, 0, 1), EXPECTED: 1 (0, 0, 1.5)."	FString
		// "RunTestFindClosestIntersection_Tangent3 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 2), EndPt(0, 0, 2), Thickness:1) = RESULT: 1, Point: (0, 0, 1), EXPECTED: 1 (0, 0, 2)."	FString
		// "RunTestFindClosestIntersection_Tangent3 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 3), EndPt(0, 0, 3), Thickness:2) = RESULT: 1, Point: (0, 0, 1), EXPECTED: 1 (0, 0, 3)."	FString
		StartPt = EndPt = Center + Chaos::TVector<T,3>(0, 0, Radius+Thickness);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Tangent3", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		StartPt = Center + Chaos::TVector<T, 3>(Radius, 0, Radius+Thickness);
		EndPt = Center + Chaos::TVector<T, 3>(-Radius, 0, Radius+Thickness);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Tangent4", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		// Radius + Thickness + 1 (miss)
		StartPt = EndPt = Center + Chaos::TVector<T,3>(0, 0, Radius+Thickness+1.0);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Tangent5", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T,3>(Radius+Thickness,0,0), false));

		StartPt = Center + Chaos::TVector<T, 3>(Radius, 0, Radius+Thickness+1.0);
		EndPt = Center + Chaos::TVector<T, 3>(-Radius, 0, Radius+Thickness+1.0);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Tangent6", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), false));
	}

	template <class T>
	void RunTestFindClosestIntersection_Shallow(
		ExampleResponse& R,
		const Chaos::TVector<T, 3>& Center,
		const T Radius,
		const T Thickness)
	{
		Chaos::TVector<T, 3> StartPt(0, 0, Radius * .999999);
		Chaos::TVector<T, 3> EndPt = StartPt;

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Shallow1 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0.999999), EndPt(0, 0, 0.999999), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (1.1, 0...	FString
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Shallow1", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(Radius+Thickness,0,0), true));

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Shallow2 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0.999999), EndPt(0, 0, 1), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		EndPt += Chaos::TVector<T, 3>(0, 0, Radius * .000001);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Shallow2", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Shallow3 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0.999999), EndPt(0, 0, 1), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		EndPt += Chaos::TVector<T, 3>(0, 0, Radius * .000001);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Shallow3", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		EndPt += Chaos::TVector<T, 3>(0, 0, Thickness);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Shallow3.1", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Shallow4 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 1), EndPt(0, 0, 0.999999), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		EndPt = StartPt;
		StartPt = EndPt + Chaos::TVector<T, 3>(0, 0, Radius * .000001);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Shallow4", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Shallow5 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 1), EndPt(0, 0, 0.999999), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		StartPt += Chaos::TVector<T, 3>(0, 0, Radius * .000001);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Shallow5", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		StartPt += Chaos::TVector<T, 3>(0, 0, Thickness);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Shallow5.1", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));
	}

	template <class T>
	void RunTestFindClosestIntersection_Mid(
		ExampleResponse& R, 
		const Chaos::TVector<T, 3>& Center, 
		const T Radius,
		const T Thickness)
	{
		Chaos::TVector<T, 3> StartPt(0,0,Radius * .5);
		Chaos::TVector<T, 3> EndPt = StartPt;

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Mid1 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0.5), EndPt(0, 0, 0.5), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (1.1, 0, 0)."	FString
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Mid1", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(Radius+Thickness,0,0), true));

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Mid2 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0.5), EndPt(0, 0, 1), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		//EndPt += Chaos::TVector<T, 3>(0, 0, Radius * .5);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Mid2", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));
		
		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Mid3 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0.5), EndPt(0, 0, 1), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		EndPt += Chaos::TVector<T, 3>(0, 0, Radius * .5);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Mid3", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Mid4 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 1), EndPt(0, 0, 0.5), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		EndPt = StartPt;
		StartPt = EndPt + Chaos::TVector<T, 3>(0, 0, Radius * .5);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Mid4", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));

		StartPt += Chaos::TVector<T, 3>(0, 0, Radius * .5);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Mid5", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T,3>(0,0,Radius+Thickness), true));
	}

	template <class T>
	void RunTestFindClosestIntersection_Deep(
		ExampleResponse& R, 
		const Chaos::TVector<T, 3>& Center, 
		const T Radius,
		const T Thickness)
	{
		Chaos::TVector<T, 3> StartPt = Center;
		Chaos::TVector<T, 3> EndPt = Center;

		// Fails w/no collision
		// "RunTestFindClosestIntersection_Deep1 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0), EndPt(0, 0, 0), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (1.1, 0, 0)."	FString
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Deep1", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T,3>(Radius+Thickness,0,0), true));

		// Fails w/no collision
		// "RunTestFindClosestIntersection_Deep2 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0), EndPt(0, 0, 0.5), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		EndPt += Chaos::TVector<T, 3>(0, 0, Radius*.5);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Deep2", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T, 3>(0, 0, Radius+Thickness), true));

		EndPt += Chaos::TVector<T, 3>(0, 0, Radius);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Deep3", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T, 3>(0, 0, Radius+Thickness), true));

		// Fails w/no collision:
		// "RunTestFindClosestIntersection_Deep4 - Sphere((0, 0, 0), 1).FindClosestIntersection(StartPt(0, 0, 0.5), EndPt(0, 0, 0), Thickness:0.1) = RESULT: 0, Point: (0, 0, 0), EXPECTED: 1 (0, 0, 1.1)."	FString
		EndPt = Center;
		StartPt += Chaos::TVector<T, 3>(0, 0, Radius*.5);
		//RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Deep4", Center, Radius, Thickness,
		//	StartPt, EndPt, 
		//	IntersectionResult(Center+Chaos::TVector<T, 3>(0, 0, Radius+Thickness), true));

		StartPt += Chaos::TVector<T, 3>(0, 0, Radius);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Deep5", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(Center+Chaos::TVector<T, 3>(0, 0, Radius+Thickness), true));

		// Miss
		StartPt = Center + Chaos::TVector<T, 3>(Radius + Thickness + 1);
		EndPt = StartPt + Chaos::TVector<T, 3>(Radius + Thickness + 1);
		RunTestFindClosestIntersectionHelper(R, "RunTestFindClosestIntersection_Deep6", Center, Radius, Thickness,
			StartPt, EndPt, 
			IntersectionResult(StartPt, false));
	}

	template <class T>
	void RunTestFindClosestIntersection(
		ExampleResponse& R, const Chaos::TVector<T,3>& Center, const T Radius)
	{
		//
		// Run different test configurations with varying thicknesses.
		//

		// Deep intersection
		RunTestFindClosestIntersection_Deep<T>(R, Center, Radius, 0.0);
		RunTestFindClosestIntersection_Deep<T>(R, Center, Radius, Radius*.1);
//		RunTestFindClosestIntersection_Deep<T>(R, Center, Radius, Radius*.5); // fails with incorrect collision points
//		RunTestFindClosestIntersection_Deep<T>(R, Center, Radius, Radius);
//		RunTestFindClosestIntersection_Deep<T>(R, Center, Radius, Radius*2);

		// Mid intersection
		RunTestFindClosestIntersection_Mid<T>(R, Center, Radius, 0.0);
		RunTestFindClosestIntersection_Mid<T>(R, Center, Radius, Radius*.1);
//		RunTestFindClosestIntersection_Mid<T>(R, Center, Radius, Radius*.5); // fails with incorrect collision points
//		RunTestFindClosestIntersection_Mid<T>(R, Center, Radius, Radius);
//		RunTestFindClosestIntersection_Mid<T>(R, Center, Radius, Radius*2);

		// Grazing intersection - all fail
		RunTestFindClosestIntersection_Shallow<T>(R, Center, Radius, 0.0);
//		RunTestFindClosestIntersection_Shallow<T>(R, Center, Radius, Radius*.1); // fails with incorrect collision points
//		RunTestFindClosestIntersection_Shallow<T>(R, Center, Radius, Radius*.5);
//		RunTestFindClosestIntersection_Shallow<T>(R, Center, Radius, Radius);
//		RunTestFindClosestIntersection_Shallow<T>(R, Center, Radius, Radius*2);

		// Tangent intersection
		RunTestFindClosestIntersection_Tangent<T>(R, Center, Radius, 0.0);
		RunTestFindClosestIntersection_Tangent<T>(R, Center, Radius, Radius*.1);
		RunTestFindClosestIntersection_Tangent<T>(R, Center, Radius, Radius*.5);
		RunTestFindClosestIntersection_Tangent<T>(R, Center, Radius, Radius);
		RunTestFindClosestIntersection_Tangent<T>(R, Center, Radius, Radius*2);
	}
	template <class T>
	void TestIntersections(ExampleResponse& R)
	{
		// At the origin
		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)0.0), (T)1.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)0.0), (T)10.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)0.0), (T)100.0);

		// Off origin
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)1.0), (T)1.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)10.0), (T)1.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)100.0), (T)1.0);

//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)1.0), (T)10.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)10.0), (T)10.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)100.0), (T)10.0);

//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)1.0), (T)100.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)10.0), (T)100.0);
//		RunTestFindClosestIntersection(R, Chaos::TVector<T, 3>((T)100.0), (T)100.0);
	}

	//==========================================================================
	// Sample points tests
	//==========================================================================

	template <class T, int d>
	void RunTestComputeSamplePoints(ExampleResponse& R, const Chaos::TSphere<T, d> &Sphere)
	{
		R.ExpectTrue(Sphere.GetType() == Chaos::ImplicitObjectType::Sphere, FString("Implicit object type is not 'sphere'."));

		Chaos::TVector<T, d> Point = Sphere.GetCenter();
		T Phi = Sphere.SignedDistance(Point);
		R.ExpectTrue(FMath::Abs(Phi + Sphere.GetRadius()) <= SMALL_NUMBER, FString("Sphere failed phi depth sanity test."));
		
		Point[0] += Sphere.GetRadius();
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

			const bool BelowCenter = Pt[2] < Sphere.GetCenter()[2] + KINDA_SMALL_NUMBER;
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

			const bool BelowCenter = Pt[2] > Sphere.GetCenter()[2] - KINDA_SMALL_NUMBER;
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
		check(!R.HasError());

		TestIntersections<T>(R);
		check(!R.HasError());

		return !R.HasError();
	}
	template bool TestImplicitSphere<float>(ExampleResponse&& R);
} // namespace GeometryCollectionExample