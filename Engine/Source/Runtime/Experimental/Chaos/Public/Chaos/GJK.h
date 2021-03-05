// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/EPA.h"
#include "Chaos/GJKShape.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Simplex.h"
#include "Chaos/Sphere.h"

#include "ChaosCheck.h"
#include "ChaosLog.h"

namespace Chaos
{

	
	/** Determines if two convex geometries overlap.
	 @A The first geometry
	 @B The second geometry
	 @BToATM The transform of B in A's local space
	 @ThicknessA The amount of geometry inflation for Geometry A(for example if the surface distance of two geometries with thickness 0 would be 2, a thickness of 0.5 would give a distance of 1.5)
	 @InitialDir The first direction we use to search the CSO
	 @ThicknessB The amount of geometry inflation for Geometry B(for example if the surface distance of two geometries with thickness 0 would be 2, a thickness of 0.5 would give a distance of 1.5)
	 @return True if the geometries overlap, False otherwise */

	template <typename T, typename TGeometryA, typename TGeometryB>
	bool GJKIntersection(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM, const T InThicknessA = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T InThicknessB = 0)
	{
		TVector<T, 3> V = -InitialDir;
		if (V.SafeNormalize() == 0)
		{
			V = TVec3<T>(-1, 0, 0);
		}

		FSimplex SimplexIDs;
		TVector<T, 3> Simplex[4];
		T Barycentric[4] = { -1,-1,-1,-1 };	//not needed, but compiler warns
		const TRotation<T, 3> AToBRotation = BToATM.GetRotation().Inverse();
		bool bTerminate;
		bool bNearZero = false;
		int NumIterations = 0;
		T PrevDist2 = FLT_MAX;
		const T ThicknessA = A.GetMargin() + InThicknessA;
		const T ThicknessB = B.GetMargin() + InThicknessB;
		const T Inflation = ThicknessA + ThicknessB + 1e-3;
		const T Inflation2 = Inflation * Inflation;
		do
		{
			if (!ensure(NumIterations++ < 32))	//todo: take this out
			{
				break;	//if taking too long just stop. This should never happen
			}
			const TVector<T, 3> NegV = -V;
			const TVector<T, 3> SupportA = A.SupportCore(NegV, A.GetMargin());
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.SupportCore(VInB, B.GetMargin());
			const TVector<T, 3> SupportB = BToATM.TransformPositionNoScale(SupportBLocal);
			const TVector<T, 3> W = SupportA - SupportB;

			if (TVector<T, 3>::DotProduct(V, W) > Inflation)
			{
				return false;
			}

			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;
			Simplex[SimplexIDs.NumVerts++] = W;

			V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric);

			T NewDist2 = V.SizeSquared();
			bNearZero = NewDist2 < Inflation2;

			//as simplices become degenerate we will stop making progress. This is a side-effect of precision, in that case take V as the current best approximation
			//question: should we take previous v in case it's better?
			const bool bMadeProgress = NewDist2 < PrevDist2;
			bTerminate = bNearZero || !bMadeProgress;

			PrevDist2 = NewDist2;

			if (!bTerminate)
			{
				V /= FMath::Sqrt(NewDist2);
			}

		} while (!bTerminate);

		return bNearZero;
		
	}

	// Calculate the penetration depth (or separating distance) of two geometries.
	//
	// Set bNegativePenetrationAllowed to false (default) if you do not care about the normal and distance when the shapes are separated. The return value
	// will be false if the shapes are separated, and the function will be faster because it does not need to determine the closest point.
	// If the shapes are overlapping, the function will return true and populate the output parameters with the contact information.
	//
	// Set bNegativePenetrationAllowed to true if you need to know the closest point on the shapes, even when they are separated. In this case,
	// we need to iterate to find the best solution even when objects are separated which is more expensive. The return value will be true as long 
	// as the algorithm was able to find a solution (i.e., the return value is not related to whether the shapes are overlapping) and the output 
	// parameters will be populated with the contact information.
	//
	// In all cases, if the function returns false the output parameters are undefined.
	//
	// OutClosestA and OutClosestB are the closest or deepest-penetrating points on the two core geometries, both in the space of A and ignoring the margin.
	//
	// Epsilon is the separation at which GJK considers the objects to be in contact or penetrating and then runs EPA. If this is
	// too small, then the renormalization of the separating vector can lead to arbitrarily wrong normals for almost-touching objects.
	//
	// NOTE: OutPenetration is the penetration including the Thickness (i.e., the actual penetration depth), but the closest points
	// returned are on the core shapes (i.e., ignoring the Thickness). If you want the closest positions on the shape surface (including
	// the Thickness) use GJKPenetration().
	//
	template <bool bNegativePenetrationAllowed = false, typename T, typename TGeometryA, typename TGeometryB>
	bool GJKPenetration(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM, T& OutPenetration, TVec3<T>& OutClosestA, TVec3<T>& OutClosestB, TVec3<T>& OutNormal, int32& OutClosestVertexIndexA, int32& OutClosestVertexIndexB, const T InThicknessA = 0.0f, const T InThicknessB = 0.0f, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T Epsilon = 1.e-3)
	{
		int32 VertexIndexA = INDEX_NONE;
		int32 VertexIndexB = INDEX_NONE;

		auto SupportAFunc = [&A, &VertexIndexA](const TVec3<T>& V)
		{
			VertexIndexA = INDEX_NONE;
			return A.SupportCore(V, A.GetMargin());
		};

		const TRotation<T, 3> AToBRotation = BToATM.GetRotation().Inverse();

		auto SupportBFunc = [&B, &BToATM, &AToBRotation, &VertexIndexB](const TVec3<T>& V)
		{
			VertexIndexB = INDEX_NONE;
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.SupportCore(VInB, B.GetMargin());
			return BToATM.TransformPositionNoScale(SupportBLocal);
		};

		//todo: refactor all of these similar functions
		TVector<T, 3> V = -InitialDir;
		if (V.SafeNormalize() == 0)
		{
			V = TVec3<T>(-1, 0, 0);
		}

		TVec3<T> As[4];
		TVec3<T> Bs[4];

		FSimplex SimplexIDs;
		TVector<T, 3> Simplex[4];
		T Barycentric[4] = { -1,-1,-1,-1 };		// Initialization not needed, but compiler warns
		TVec3<T> Normal = -V;					// Remember the last good normal (i.e. don't update it if separation goes less than Epsilon and we can no longer normalize)
		bool bIsDegenerate = false;				// True if GJK cannot make any more progress
		bool bIsContact = false;				// True if shapes are within Epsilon or overlapping - GJK cannot provide a solution
		int NumIterations = 0;
		T Distance = FLT_MAX;
		const T ThicknessA = InThicknessA + A.GetMargin();
		const T ThicknessB = InThicknessB + B.GetMargin();
		const T SeparatedDistance = ThicknessA + ThicknessB + Epsilon;
		while (!bIsContact && !bIsDegenerate)
		{
			if (!ensure(NumIterations++ < 32))	//todo: take this out
			{
				break;	//if taking too long just stop. This should never happen
			}
			const TVector<T, 3> NegV = -V;
			const TVector<T, 3> SupportA = SupportAFunc(NegV);
			const TVector<T, 3> SupportB = SupportBFunc(V);
			const TVector<T, 3> W = SupportA - SupportB;

			const T VW = TVector<T, 3>::DotProduct(V, W);
			if (!bNegativePenetrationAllowed && (VW > SeparatedDistance))
			{
				// We are separated and don't care about the distance - we can stop now
				return false;
			}

			// If we didn't move to at least ConvergedDistance or closer, assume we have reached a minimum
			const T ConvergenceTolerance = 1.e-4f;
			const T ConvergedDistance = (1.0f - ConvergenceTolerance) * Distance;
			if (VW > ConvergedDistance)
			{
				// We have reached a solution - use the results from the last iteration
				break;
			}

			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;
			As[SimplexIDs.NumVerts] = SupportA;
			Bs[SimplexIDs.NumVerts] = SupportB;
			Simplex[SimplexIDs.NumVerts++] = W;

			V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric, As, Bs);
			T NewDistance = V.Size();

			// Are we overlapping or too close for GJK to get a good result?
			bIsContact = (NewDistance < Epsilon);

			// If we did not get closer in this iteration, we are in a degenerate situation
			bIsDegenerate = (NewDistance >= Distance);

			if (!bIsContact)
			{
				V /= NewDistance;
				Normal = -V;
			}
			Distance = NewDistance;
		}

		if (bIsContact)
		{
			// We did not converge or we detected an overlap situation, so run EPA to get contact data
			TArray<TVec3<T>> VertsA;
			TArray<TVec3<T>> VertsB;
			VertsA.Reserve(8);
			VertsB.Reserve(8);
			for (int i = 0; i < SimplexIDs.NumVerts; ++i)
			{
				VertsA.Add(As[i]);
				VertsB.Add(Bs[i]);
			}

			T Penetration;
			TVec3<T> MTD, ClosestA, ClosestBInA;
			const EEPAResult EPAResult = EPA(VertsA, VertsB, SupportAFunc, SupportBFunc, Penetration, MTD, ClosestA, ClosestBInA);
			
			switch (EPAResult)
			{
			case EEPAResult::MaxIterations:
				// Possibly a solution but with unknown error. Just return the last EPA state. 
				// Fall through...
			case EEPAResult::Ok:
				// EPA has a solution - return it now
				OutNormal = MTD;
				OutPenetration = Penetration + ThicknessA + ThicknessB;
				OutClosestA = ClosestA + MTD * ThicknessA;
				OutClosestB = ClosestBInA - MTD * ThicknessB;
				OutClosestVertexIndexA = VertexIndexA;
				OutClosestVertexIndexB = VertexIndexB;
				return true;
			case EEPAResult::BadInitialSimplex:
				// The origin is outside the simplex. Must be a touching contact and EPA setup will have calculated the normal
				// and penetration but we keep the position generated by GJK.
				Normal = MTD;
				Distance = -Penetration;
				break;
			case EEPAResult::Degenerate:
				// We hit a degenerate simplex condition and could not reach a solution so use whatever near touching point GJK came up with.
				// The result from EPA under these circumstances is not usable.
				//UE_LOG(LogChaos, Warning, TEXT("EPA Degenerate Case"));
				break;
			}
		}

		// @todo(chaos): handle the case where EPA hit a degenerate triangle in the simplex.
		// Currently we return a touching contact with the last position and normal from GJK. We could run SAT instead.

		// GJK converged or we have a touching contact
		{
			TVector<T, 3> ClosestA(0);
			TVector<T, 3> ClosestBInA(0);
			for (int i = 0; i < SimplexIDs.NumVerts; ++i)
			{
				ClosestA += As[i] * Barycentric[i];
				ClosestBInA += Bs[i] * Barycentric[i];
			}

			OutNormal = Normal;

			T Penetration = ThicknessA + ThicknessB - Distance;
			OutPenetration = Penetration;
			OutClosestA = ClosestA + Normal * ThicknessA;
			OutClosestB = ClosestBInA - Normal * ThicknessB;
			OutClosestVertexIndexA = VertexIndexA;
			OutClosestVertexIndexB = VertexIndexB;

			// If we don't care about separation distance/normal, the return value is true if we are overlapping, false otherwise.
			// If we do care about seperation distance/normal, the return value is true if we found a solution.
			// @todo(chaos): we should pass back failure for the degenerate case so we can decide how to handle it externally.
			return (bNegativePenetrationAllowed || (Penetration >= 0.0f));
		}
	}


	/** Sweeps one geometry against the other
	 @A The first geometry
	 @B The second geometry
	 @StartTM B's starting configuration in A's local space
	 @RayDir The ray's direction (normalized)
	 @RayLength The ray's length
	 @OutTime The time along the ray when the objects first overlap
	 @OutPosition The first point of impact (in A's local space) when the objects first overlap. Invalid if time of impact is 0
	 @OutNormal The impact normal (in A's local space) when the objects first overlap. Invalid if time of impact is 0
	 @ThicknessA The amount of geometry inflation for Geometry A (for example if the surface distance of two geometries with thickness 0 would be 2, a thickness of 0.5 would give a distance of 1.5)
	 @InitialDir The first direction we use to search the CSO
	 @ThicknessB The amount of geometry inflation for Geometry B (for example if the surface distance of two geometries with thickness 0 would be 2, a thickness of 0.5 would give a distance of 1.5)
	 @return True if the geometries overlap during the sweep, False otherwise 
	 @note If A overlaps B at the start of the ray ("initial overlap" condition) then this function returns true, and sets OutTime = 0, but does not set any other output variables.
	 */

	template <typename T, typename TGeometryA, typename TGeometryB>
	bool GJKRaycast(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& RayDir, const T RayLength,
		T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, const T ThicknessA = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T ThicknessB = 0)
	{
		ensure(FMath::IsNearlyEqual(RayDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		ensure(RayLength > 0);
		check(A.IsConvex() && B.IsConvex());
		const TVector<T, 3> StartPoint = StartTM.GetLocation();

		TVector<T, 3> Simplex[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };
		TVector<T, 3> As[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };
		TVector<T, 3> Bs[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };

		T Barycentric[4] = { -1,-1,-1,-1 };	//not needed, but compiler warns

		FSimplex SimplexIDs;
		const TRotation<T, 3> BToARotation = StartTM.GetRotation();
		const TRotation<T, 3> AToBRotation = BToARotation.Inverse();
		TVector<T, 3> SupportA = A.Support(InitialDir, ThicknessA);	//todo: use Thickness on quadratic geometry
		As[0] = SupportA;

		const TVector<T, 3> InitialDirInB = AToBRotation * (-InitialDir);
		const TVector<T, 3> InitialSupportBLocal = B.Support(InitialDirInB, ThicknessB);
		TVector<T, 3> SupportB = BToARotation * InitialSupportBLocal;
		Bs[0] = SupportB;

		T Lambda = 0;
		TVector<T, 3> X = StartPoint;
		TVector<T, 3> Normal(0);
		TVector<T, 3> V = X - (SupportA - SupportB);

		bool bTerminate;
		bool bNearZero = false;
		bool bDegenerate = false;
		int NumIterations = 0;
		T InGJKPreDist2 = TNumericLimits<T>::Max();
		do
		{
			//if (!ensure(NumIterations++ < 32))	//todo: take this out
			if (!(NumIterations++ < 32))	//todo: take this out
			{
				break;	//if taking too long just stop. This should never happen
			}

			SupportA = A.Support(V, ThicknessA);	//todo: add thickness to quadratic geometry to avoid quadratic vs quadratic when possible
			const TVector<T, 3> VInB = AToBRotation * (-V);
			const TVector<T, 3> SupportBLocal = B.Support(VInB, ThicknessB);
			SupportB = BToARotation * SupportBLocal;
			const TVector<T, 3> P = SupportA - SupportB;
			const TVector<T, 3> W = X - P;
			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;	//is this needed?
			As[SimplexIDs.NumVerts] = SupportA;
			Bs[SimplexIDs.NumVerts] = SupportB;

			const T VDotW = TVector<T, 3>::DotProduct(V, W);
			if (VDotW > 0)
			{
				const T VDotRayDir = TVector<T, 3>::DotProduct(V, RayDir);
				if (VDotRayDir >= 0)
				{
					return false;
				}

				const T PreLambda = Lambda;	//use to check for no progress
				// @todo(ccaulfield): this can still overflow - the comparisons against zero above should be changed (though not sure to what yet)
				Lambda = Lambda - VDotW / VDotRayDir;
				if (Lambda > PreLambda)
				{
					if (Lambda > RayLength)
					{
						return false;
					}

					const TVector<T, 3> OldX = X;
					X = StartPoint + Lambda * RayDir;
					Normal = V;

					//Update simplex from (OldX - P) to (X - P)
					const TVector<T, 3> XMinusOldX = X - OldX;
					Simplex[0] += XMinusOldX;
					Simplex[1] += XMinusOldX;
					Simplex[2] += XMinusOldX;
					Simplex[SimplexIDs.NumVerts++] = X - P;

					InGJKPreDist2 = TNumericLimits<T>::Max();	//translated origin so restart gjk search
				}
			}
			else
			{
				Simplex[SimplexIDs.NumVerts++] = W;	//this is really X - P which is what we need for simplex computation
			}

			V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric, As, Bs);

			T NewDist2 = V.SizeSquared();	//todo: relative error
			bNearZero = NewDist2 < 1e-6;
			bDegenerate = NewDist2 >= InGJKPreDist2;
			InGJKPreDist2 = NewDist2;
			bTerminate = bNearZero || bDegenerate;

		} while (!bTerminate);

		OutTime = Lambda;

		if (Lambda > 0)
		{
			OutNormal = Normal.GetUnsafeNormal();
			TVector<T, 3> ClosestA(0);
			TVector<T, 3> ClosestB(0);

			for (int i = 0; i < SimplexIDs.NumVerts; ++i)
			{
				ClosestB += Bs[i] * Barycentric[i];
			}
			const TVector<T, 3> ClosestLocal = ClosestB;

			OutPosition = StartPoint + RayDir * Lambda + ClosestLocal;
		}

		return true;
	}


	/** Sweeps one geometry against the other
	 @A The first geometry
	 @B The second geometry
	 @StartTM B's starting configuration in A's local space
	 @RayDir The ray's direction (normalized)
	 @RayLength The ray's length
	 @OutTime The time along the ray when the objects first overlap
	 @OutPosition The first point of impact (in A's local space) when the objects first overlap. Invalid if time of impact is 0
	 @OutNormal The impact normal (in A's local space) when the objects first overlap. Invalid if time of impact is 0
	 @ThicknessA The amount of geometry inflation for Geometry A (for example a capsule with radius 5 could pass in its core segnment and a thickness of 5)
	 @InitialDir The first direction we use to search the CSO
	 @ThicknessB The amount of geometry inflation for Geometry B (for example a sphere with radius 5 could pass in its center point and a thickness of 5)
	 @return True if the geometries overlap during the sweep, False otherwise 
	 @note If A overlaps B at the start of the ray ("initial overlap" condition) then this function returns true, and sets OutTime = 0, but does not set any other output variables.
	 */

	template <typename T, typename TGeometryA, typename TGeometryB>
	bool GJKRaycast2(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& StartTM, const TVector<T, 3>& RayDir, const T RayLength,
		T& OutTime, TVector<T, 3>& OutPosition, TVector<T, 3>& OutNormal, const T GivenThicknessA = 0, bool bComputeMTD = false, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T GivenThicknessB = 0)
	{
		ensure(FMath::IsNearlyEqual(RayDir.SizeSquared(), 1, KINDA_SMALL_NUMBER));
		ensure(RayLength > 0);

		// Margin selection logic: we only need a small margin for sweeps since we only move the sweeping object
		// to the point where it just touches.
		// Spheres and Capsules: always use the core shape and full "margin" because it represents the radius
		// Sphere/Capsule versus OtherShape: no margin on other
		// OtherShape versus OtherShape: use margin of the smaller shape, zero margin on the other
		const T RadiusA = A.GetRadius();
		const T RadiusB = B.GetRadius();
		const bool bHasRadiusA = RadiusA > 0;
		const bool bHasRadiusB = RadiusB > 0;

		// The sweep margins if required. Only one can be non-zero (we keep the smaller one)
		const T SweepMarginScale = 0.05f;
		const bool bAIsSmallest = A.GetMargin() < B.GetMargin();
		const T SweepMarginA = (bHasRadiusA || bHasRadiusB) ? 0.0f : (bAIsSmallest ? SweepMarginScale * A.GetMargin() : 0.0f);
		const T SweepMarginB = (bHasRadiusA || bHasRadiusB) ? 0.0f : (bAIsSmallest ? 0.0f : SweepMarginScale * B.GetMargin());

		// Net margin (note: both SweepMargins are zero if either Radius is non-zero, and only one SweepMargin can be non-zero)
		const T MarginA = RadiusA + SweepMarginA;
		const T MarginB = RadiusB + SweepMarginB;

		const TVector<T, 3> StartPoint = StartTM.GetLocation();

		TVector<T, 3> Simplex[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };
		TVector<T, 3> As[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };
		TVector<T, 3> Bs[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };

		T Barycentric[4] = { -1,-1,-1,-1 };	//not needed, but compiler warns
		const T Inflation = MarginA + MarginB;
		const T Inflation2 = Inflation*Inflation + 1e-6;

		FSimplex SimplexIDs;
		const TRotation<T, 3> BToARotation = StartTM.GetRotation();
		const TRotation<T, 3> AToBRotation = BToARotation.Inverse();

		auto SupportAFunc = [&A, MarginA](const TVec3<T>& V)
		{
			return A.SupportCore(V, MarginA);
		};

		auto SupportBFunc = [&B, MarginB, &AToBRotation, &BToARotation](const TVec3<T>& V)
		{
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.SupportCore(VInB, MarginB);
			return BToARotation * SupportBLocal;
		};

		TVector<T, 3> SupportA = SupportAFunc(InitialDir);
		As[0] = SupportA;

		TVector<T, 3> SupportB = SupportBFunc(-InitialDir);
		Bs[0] = SupportB;

		T Lambda = 0;
		TVector<T, 3> X = StartPoint;
		TVector<T, 3> V = X - (SupportA - SupportB);
		TVector<T, 3> Normal(0,0,1);

		const T InitialPreDist2 = V.SizeSquared();
		constexpr T Eps2 = 1e-6;
		//mtd needs to find closest point even in inflation region, so can only skip if we found the closest points
		bool bCloseEnough = InitialPreDist2 < Inflation2 && (!bComputeMTD || InitialPreDist2 < Eps2);
		bool bDegenerate = false;
		bool bTerminate = bCloseEnough;
		bool bInflatedCloseEnough = bCloseEnough;
		int NumIterations = 0;
		T InGJKPreDist2 = TNumericLimits<T>::Max();
		while (!bTerminate)
		{
			//if (!ensure(NumIterations++ < 32))	//todo: take this out
			if (!(NumIterations++ < 32))	//todo: take this out
			{
				break;	//if taking too long just stop. This should never happen
			}

			V = V.GetUnsafeNormal();

			SupportA = SupportAFunc(V);
			SupportB = SupportBFunc(-V);
			const TVector<T, 3> P = SupportA - SupportB;
			const TVector<T, 3> W = X - P;
			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;	//is this needed?
			As[SimplexIDs.NumVerts] = SupportA;
			Bs[SimplexIDs.NumVerts] = SupportB;

			const T VDotW = TVector<T, 3>::DotProduct(V, W);

			if (VDotW > Inflation)
			{
				const T VDotRayDir = TVector<T, 3>::DotProduct(V, RayDir);
				if (VDotRayDir >= 0)
				{
					return false;
				}

				const T PreLambda = Lambda;	//use to check for no progress
				// @todo(ccaulfield): this can still overflow - the comparisons against zero above should be changed (though not sure to what yet)
				Lambda = Lambda - (VDotW - Inflation) / VDotRayDir;
				if (Lambda > PreLambda)
				{
					if (Lambda > RayLength)
					{
						return false;
					}

					const TVector<T, 3> OldX = X;
					X = StartPoint + Lambda * RayDir;
					Normal = V;

					//Update simplex from (OldX - P) to (X - P)
					const TVector<T, 3> XMinusOldX = X - OldX;
					Simplex[0] += XMinusOldX;
					Simplex[1] += XMinusOldX;
					Simplex[2] += XMinusOldX;
					Simplex[SimplexIDs.NumVerts++] = X - P;

					InGJKPreDist2 = TNumericLimits<T>::Max();	//translated origin so restart gjk search
					bInflatedCloseEnough = false;
				}
			}
			else
			{
				Simplex[SimplexIDs.NumVerts++] = W;	//this is really X - P which is what we need for simplex computation
			}

			if (bInflatedCloseEnough && VDotW >= 0)
			{
				//Inflated shapes are close enough, but we want MTD so we need to find closest point on core shape
				const T VDotW2 = VDotW * VDotW;
				bCloseEnough = InGJKPreDist2 <= Eps2 + VDotW2;	//todo: relative error
			}

			if (!bCloseEnough)
			{
				V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric, As, Bs);
				T NewDist2 = V.SizeSquared();	//todo: relative error
				bCloseEnough = NewDist2 < Inflation2;
				bDegenerate = NewDist2 >= InGJKPreDist2;
				InGJKPreDist2 = NewDist2;


				if (bComputeMTD && bCloseEnough && Lambda == 0 && InGJKPreDist2 > 1e-6 && Inflation2 > 1e-6 && SimplexIDs.NumVerts < 4)
				{
					//For mtd of inflated shapes we have to find the closest point, so we have to keep going
					bCloseEnough = false;
					bInflatedCloseEnough = true;
				}
			}
			else
			{
				//It must be that we want MTD and we can terminate. However, we must make one final call to fixup the simplex
				V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric, As, Bs);
			}
			bTerminate = bCloseEnough || bDegenerate;
		}

		OutTime = Lambda;

		if (Lambda > 0)
		{
			OutNormal = Normal;
			TVector<T, 3> ClosestB(0);

			for (int i = 0; i < SimplexIDs.NumVerts; ++i)
			{
				ClosestB += Bs[i] * Barycentric[i];
			}
			const TVector<T, 3> ClosestLocal = ClosestB - OutNormal * MarginB;

			OutPosition = StartPoint + RayDir * Lambda + ClosestLocal;
		}
		else if (bComputeMTD)
		{
			// If Inflation == 0 we would expect GJKPreDist2 to be 0
			// However, due to precision we can still end up with GJK failing.
			// When that happens fall back on EPA
			if (Inflation > 0 && InGJKPreDist2 > 1e-6 && InGJKPreDist2 < TNumericLimits<T>::Max())
			{
				OutNormal = Normal;
				TVector<T, 3> ClosestA(0);
				TVector<T, 3> ClosestB(0);

				if (NumIterations)
				{
					for (int i = 0; i < SimplexIDs.NumVerts; ++i)
					{
						ClosestA += As[i] * Barycentric[i];
						ClosestB += Bs[i] * Barycentric[i];
					}
				}
				else
				{
					//didn't even go into gjk loop
					ClosestA = As[0];
					ClosestB = Bs[0];
				}
				
				const TVec3<T> ClosestBInA = StartPoint + ClosestB;
				const T InGJKPreDist = FMath::Sqrt(InGJKPreDist2);
				OutNormal = V.GetUnsafeNormal();

				const T Penetration = FMath::Clamp<T>(MarginA + MarginB - InGJKPreDist, 0, TNumericLimits<T>::Max());
				const TVector<T, 3> ClosestLocal = ClosestB - OutNormal * MarginB;

				OutPosition = StartPoint + ClosestLocal + OutNormal * Penetration;
				OutTime = -Penetration;
			}
			else
			{
				//use EPA
				TArray<TVec3<T>> VertsA;
				TArray<TVec3<T>> VertsB;

				VertsA.Reserve(8);
				VertsB.Reserve(8);

				if (NumIterations)
				{
					for (int i = 0; i < SimplexIDs.NumVerts; ++i)
					{
						VertsA.Add(As[i]);
						const TVec3<T> BAtOrigin = Bs[i] + X;
						VertsB.Add(BAtOrigin);
					}


					auto SupportBAtOriginFunc = [&](const TVec3<T>& Dir)
					{
						const TVector<T, 3> DirInB = AToBRotation * Dir;
						const TVector<T, 3> SupportBLocal = B.SupportCore(DirInB, MarginB);
						return StartTM.TransformPositionNoScale(SupportBLocal);
					};

					T Penetration;
					TVec3<T> MTD, ClosestA, ClosestBInA;
					const EEPAResult EPAResult = EPA(VertsA, VertsB, SupportAFunc, SupportBAtOriginFunc, Penetration, MTD, ClosestA, ClosestBInA);
					if (IsEPASuccess(EPAResult))
					{
						OutNormal = MTD;
						OutTime = -Penetration - Inflation;
						OutPosition = ClosestA;
					}
					//else if (IsEPADegenerate(EPAResult))
					//{
					//	// @todo(chaos): handle degenerate EPA condition
					//}
					else
					{
						//assume touching hit
						OutTime = -Inflation;
						OutNormal = MTD;
						OutPosition = As[0] + OutNormal * MarginA;
					}
				}
				else
				{
					//didn't even go into gjk loop, touching hit
					OutTime = -Inflation;
					OutNormal = { 0,0,1 };
					OutPosition = As[0] + OutNormal * MarginA;
				}
			}
		}
		else
		{
			// Initial overlap without MTD. These properties are not valid, but assigning them anyway so they don't contain NaNs and cause issues in invoking code.
			OutNormal = { 0,0,1 };
			OutPosition = { 0,0,0 };
		}

		return true;
	}

	/**
	 * Used by GJKDistance. It must return a vector in the Minkowski sum A - B. In principle this can be the vector of any point
	 * in A to any point in B, but some choices will cause GJK to minimize faster (e.g., for two spheres, we can easily calculate
	 * the actual separating vector and GJK will converge immediately).
	 */
	template <typename T, typename TGeometryA, typename TGeometryB>
	TVector<T, 3> GJKDistanceInitialV(const TGeometryA& A, T MarginA, const TGeometryB& B, T MarginB, const TRigidTransform<T, 3>& BToATM)
	{
		const TVec3<T> V = -BToATM.GetTranslation();
		const TVector<T, 3> SupportA = A.SupportCore(-V, MarginA);
		const TVector<T, 3> VInB = BToATM.GetRotation().Inverse() * V;
		const TVector<T, 3> SupportBLocal = B.SupportCore(VInB, MarginB);
		const TVector<T, 3> SupportB = BToATM.TransformPositionNoScale(SupportBLocal);
		return SupportA - SupportB;
	}

	/**
	 * Used by GJKDistance. Specialization for sphere-sphere gives correct result immediately.
	 */
	template <typename T>
	TVector<T, 3> GJKDistanceInitialV(const TSphere<T, 3>& A, const TSphere<T, 3>& B, const TRigidTransform<T, 3>& BToATM)
	{
		TVector<T, 3> Delta = A.GetCenter() - (B.GetCenter() + BToATM.GetTranslation());
		return Delta;
	}

	// Status of a call to GJKDistance
	enum class EGJKDistanceResult
	{
		// The shapes are separated by a positive amount and all outputs have valid values
		Separated,

		// The shapes are overlapping by less than the net margin and all outputs have valid values (with a negative separation)
		Contact,

		// The shapes are overlapping by more than the net margin and all outputs are invalid
		DeepContact,
	};

	/**
	 * Find the distance and nearest points on two convex geometries A and B.
	 * All calculations are performed in the local-space of object A, and the transform from B-space to A-space must be provided.
	 * For algorithm see "A Fast and Robust GJK Implementation for Collision Detection of Convex Objects", Gino Van Deb Bergen, 1999.
	 * @note This algorithm aborts if objects are overlapping and it does not initialize the out parameters.
	 *
	 * @param A The first object.
	 * @param B The second object.
	 * @param BToATM A transform taking vectors in B-space to A-space
	 * @param B The second object.
	 * @param OutDistance if returns true, the minimum distance between A and B, otherwise not modified.
	 * @param OutNearestA if returns true, the near point on A in local-space, otherwise not modified.
	 * @param OutNearestB if returns true, the near point on B in local-space, otherwise not modified.
	 * @param Epsilon The algorithm terminates when the iterative distance reduction gets below this threshold.
	 * @param MaxIts A limit on the number of iterations. Results may be approximate if this is too low.
	 * @return EGJKDistanceResult - see comments on the enum
	 */
	template <typename T, typename TGeometryA, typename TGeometryB>
	EGJKDistanceResult GJKDistance(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM, T& OutDistance, TVector<T, 3>& OutNearestA, TVector<T, 3>& OutNearestB, TVector<T, 3>& OutNormalA, const T Epsilon = (T)1e-3, const int32 MaxIts = 16)
	{
		check(A.IsConvex() && B.IsConvex());

		FSimplex SimplexIDs;
		TVector<T, 3> Simplex[4], SimplexA[4], SimplexB[4];
		T Barycentric[4] = { -1, -1, -1, -1 };

		const TRotation<T, 3> AToBRotation = BToATM.GetRotation().Inverse();
		const T AMargin = A.GetMargin();
		const T BMargin = B.GetMargin();
		T Mu = 0;

		// Select an initial vector in Minkowski(A - B)
		TVector<T, 3> V = GJKDistanceInitialV(A, AMargin, B, BMargin, BToATM);
		T VLen = V.Size();

		int32 It = 0;
		while (VLen > Epsilon)
		{
			// Find a new point in A-B that is closer to the origin
			// NOTE: we do not use support thickness here. Thickness is used when separating objects
			// so that GJK can find a solution, but that can be added in a later step.
			const TVector<T, 3> SupportA = A.SupportCore(-V, AMargin);
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.SupportCore(VInB, BMargin);
			const TVector<T, 3> SupportB = BToATM.TransformPositionNoScale(SupportBLocal);
			const TVector<T, 3> W = SupportA - SupportB;

			T D = TVector<T, 3>::DotProduct(V, W) / VLen;
			Mu = FMath::Max(Mu, D);

			// See if we are still making progress toward the origin
			bool bCloseEnough = ((VLen - Mu) < Epsilon);
			if (bCloseEnough || (++It > MaxIts))
			{
				// We have reached the minimum to within tolerance. Or we have reached max iterations, in which
				// case we (probably) have a solution but with an error larger than Epsilon (technically we could be missing
				// the fact that we were going to eventually find the origin, but it'll be a close call so the approximation
				// is still good enough).
				if (SimplexIDs.NumVerts == 0)
				{
					// Our initial guess of V was already the minimum separating vector
					OutNearestA = SupportA;
					OutNearestB = SupportBLocal;
				}
				else
				{
					// The simplex vertices are the nearest point/line/face
					OutNearestA = TVector<T, 3>(0, 0, 0);
					OutNearestB = TVector<T, 3>(0, 0, 0);
					for (int32 VertIndex = 0; VertIndex < SimplexIDs.NumVerts; ++VertIndex)
					{
						int32 WIndex = SimplexIDs[VertIndex];
						check(Barycentric[WIndex] >= (T)0);
						OutNearestA += Barycentric[WIndex] * SimplexA[WIndex];
						OutNearestB += Barycentric[WIndex] * SimplexB[WIndex];
					}
				}
				const TVector<T, 3> NormalA = -V / VLen;
				const TVector<T, 3> NormalB = VInB / VLen;
				OutDistance = VLen - (AMargin + BMargin);
				OutNearestA += AMargin * NormalA;
				OutNearestB += BMargin * NormalB;
				OutNormalA = NormalA;

				return (OutDistance >= 0.0f) ? EGJKDistanceResult::Separated : EGJKDistanceResult::Contact;
			}

			// Add the new vertex to the simplex
			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;
			Simplex[SimplexIDs.NumVerts] = W;
			SimplexA[SimplexIDs.NumVerts] = SupportA;
			SimplexB[SimplexIDs.NumVerts] = SupportBLocal;
			++SimplexIDs.NumVerts;

			// Find the closest point to the origin on the simplex, and update the simplex to eliminate unnecessary vertices
			V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric, SimplexA, SimplexB);
			VLen = V.Size();
		}

		// Our geometries overlap - we did not set any outputs
		return EGJKDistanceResult::DeepContact;
	}





	// Assumes objects are already intersecting, computes a minimum translation
	// distance, deepest penetration positions on each body, and approximates
	// a penetration normal and minimum translation distance.
	//
	// TODO: We want to re-visit how these functions work. Probably should be
	// embedded in GJKOverlap and GJKRaycast so that secondary queries are unnecessary.
	template <typename T, typename TGeometryA, typename TGeometryB>
	bool GJKPenetrationTemp(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM, TVector<T, 3>& OutPositionA, TVector<T, 3>& OutPositionB, TVector<T, 3>& OutNormal, T& OutDistance, const T ThicknessA = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T ThicknessB = 0, const T Epsilon = (T)1e-6, const int32 MaxIts = 16)
	{
		//
		// TODO: General case for MTD determination.
		//
		ensure(false);
		OutPositionA = TVector<T, 3>(0.f);
		OutPositionB = TVector<T, 3>(0.f);
		OutNormal = TVector<T, 3>(0.f, 0.f, 1.f);
		OutDistance = 0.f;
		return GJKIntersection(A, B, BToATM, ThicknessA, InitialDir, ThicknessB);
	}

	// Specialization for when getting MTD against a capsule.
	template <typename T, typename TGeometryA>
	bool GJKPenetrationTemp(const TGeometryA& A, const FCapsule& B, const TRigidTransform<T, 3>& BToATM, TVector<T, 3>& OutPositionA, TVector<T, 3>& OutPositionB, TVector<T, 3>& OutNormal, T& OutDistance, const T ThicknessA = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T ThicknessB = 0, const T Epsilon = (T)1e-6, const int32 MaxIts = 16)
	{
		T SegmentDistance;
		const TSegment<T>& Segment = B.GetSegment();
		const T MarginB = B.GetRadius();
		TVector<T, 3> PositionBInB;
		if (GJKDistance(A, Segment, BToATM, SegmentDistance, OutPositionA, PositionBInB, Epsilon, MaxIts))
		{
			OutPositionB = BToATM.TransformPosition(PositionBInB);
			OutNormal
				= ensure(SegmentDistance > TNumericLimits<T>::Min())
				? (OutPositionB - OutPositionA) / SegmentDistance
				: TVector<T, 3>(0.f, 0.f, 1.f);
			OutPositionB -= OutNormal * MarginB;
			OutDistance = SegmentDistance - MarginB;

			if (OutDistance > 0.f)
			{
				// In this case, our distance calculation says we're not penetrating.
				//
				// TODO: check(false)! This shouldn't happen.
				// It probably won't happen anymore if we warm-start GJKDistance
				// with a polytope.
				//
				OutDistance = 0.f;
				return false;
			}

			return true;
		}
		else
		{
			// TODO: Deep penetration - do EPA
			ensure(false);
			return true;
		}

		return false;
	}

}
