// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Simplex.h"
#include "Chaos/Capsule.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/EPA.h"

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
			const TVector<T, 3> SupportA = A.Support2(NegV);
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.Support2(VInB);
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

	template <typename T, typename TGeometryA, typename TGeometryB>
	bool GJKPenetration(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM, T& OutPenetration, TVec3<T>& OutClosestA, TVec3<T>& OutClosestB, TVec3<T>& OutNormal, const T InThicknessA = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T InThicknessB = 0)
	{
		auto SupportAFunc = [&A](const TVec3<T>& V)
		{
			return A.Support2(V);
		};

		const TRotation<T, 3> AToBRotation = BToATM.GetRotation().Inverse();


		auto SupportBFunc = [&B, &BToATM, &AToBRotation](const TVec3<T>& V)
		{
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.Support2(VInB);
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
		T Barycentric[4] = { -1,-1,-1,-1 };	//not needed, but compiler warns
		bool bTerminate;
		bool bNearZero = false;
		int NumIterations = 0;
		T PrevDist2 = FLT_MAX;
		const T ThicknessA = A.GetMargin() + InThicknessA;
		const T ThicknessB = B.GetMargin() + InThicknessB;
		const T Inflation = ThicknessA + ThicknessB + 1e-3;
		const T Inflation2 = Inflation * Inflation;
		const T Eps2 = 1e-6;
		do
		{
			if (!ensure(NumIterations++ < 32))	//todo: take this out
			{
				break;	//if taking too long just stop. This should never happen
			}
			const TVector<T, 3> NegV = -V;
			const TVector<T, 3> SupportA = SupportAFunc(NegV);
			const TVector<T, 3> SupportB = SupportBFunc(V);
			const TVector<T, 3> W = SupportA - SupportB;

			if (TVector<T, 3>::DotProduct(V, W) > Inflation)
			{
				return false;
			}

			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;
			As[SimplexIDs.NumVerts] = SupportA;
			Bs[SimplexIDs.NumVerts] = SupportB;
			Simplex[SimplexIDs.NumVerts++] = W;

			V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric, As, Bs);

			T NewDist2 = V.SizeSquared();
			bNearZero = NewDist2 < Eps2;	//want to get the closest point for MTD

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

		if (PrevDist2 > Eps2)
		{
			//generally this happens when shapes are inflated.
			TVector<T, 3> ClosestA(0);
			TVector<T, 3> ClosestBInA(0);

			for (int i = 0; i < SimplexIDs.NumVerts; ++i)
			{
				ClosestA += As[i] * Barycentric[i];
				ClosestBInA += Bs[i] * Barycentric[i];
			}

			
			const T PreDist = FMath::Sqrt(PrevDist2);
			OutNormal = (ClosestBInA - ClosestA).GetUnsafeNormal();	//question: should we just use PreDist2?
			const T Penetration = ThicknessA + ThicknessB - PreDist;
			OutPenetration = Penetration;
			OutClosestA = ClosestA + OutNormal * ThicknessA;
			OutClosestB = ClosestBInA - OutNormal * ThicknessB;

		}
		else
		{
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
			if (EPA(VertsA, VertsB, SupportAFunc, SupportBFunc, Penetration, MTD, ClosestA, ClosestBInA) != EPAResult::BadInitialSimplex)
			{
				OutNormal = MTD;
				OutPenetration = Penetration + ThicknessA + ThicknessB;
				OutClosestA = ClosestA + OutNormal * ThicknessA;
				OutClosestB = ClosestBInA - OutNormal * ThicknessB;
			}
			else
			{
				//assume touching hit

				ClosestA = TVec3<T>(0);
				ClosestBInA = TVec3<T>(0);

				for (int i = 0; i < SimplexIDs.NumVerts; ++i)
				{
					ClosestA += As[i] * Barycentric[i];
					ClosestBInA += Bs[i] * Barycentric[i];
				}

				OutPenetration = ThicknessA + ThicknessB;
				OutNormal = { 0,0,1 };
				OutClosestA = ClosestA + OutNormal * ThicknessA;
				OutClosestB = ClosestBInA - OutNormal * ThicknessB;
				return false;
			}
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
		const T ThicknessA = A.GetMargin();
		const T ThicknessB = B.GetMargin();

		const TVector<T, 3> StartPoint = StartTM.GetLocation();

		TVector<T, 3> Simplex[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };
		TVector<T, 3> As[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };
		TVector<T, 3> Bs[4] = { TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0), TVector<T,3>(0) };

		T Barycentric[4] = { -1,-1,-1,-1 };	//not needed, but compiler warns
		const T Inflation = ThicknessA + ThicknessB;
		const T Inflation2 = Inflation*Inflation + 1e-6;

		FSimplex SimplexIDs;
		const TRotation<T, 3> BToARotation = StartTM.GetRotation();
		const TRotation<T, 3> AToBRotation = BToARotation.Inverse();

		auto SupportAFunc = [&A](const TVec3<T>& V)
		{
			return A.Support2(V);
		};

		auto SupportBFunc = [&B, &AToBRotation, &BToARotation](const TVec3<T>& V)
		{
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.Support2(VInB);
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

			SupportA = SupportAFunc(V);
			SupportB = SupportBFunc(-V);
			const TVector<T, 3> P = SupportA - SupportB;
			const TVector<T, 3> W = X - P;
			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;	//is this needed?
			As[SimplexIDs.NumVerts] = SupportA;
			Bs[SimplexIDs.NumVerts] = SupportB;

			V = V.GetUnsafeNormal();

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


				if (bComputeMTD && bCloseEnough && Lambda == 0 && Inflation2 > 1e-6 && SimplexIDs.NumVerts < 4)
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
			const TVector<T, 3> ClosestLocal = ClosestB - OutNormal * ThicknessB;

			OutPosition = StartPoint + RayDir * Lambda + ClosestLocal;
		}
		else if (bComputeMTD)
		{
			if (InGJKPreDist2 > 1e-6 && InGJKPreDist2 < TNumericLimits<T>::Max())
			{
				ensure(Inflation > 0);	//shouldn't end up here if there is no inflation
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
				
				const TVec3<T> ClosestBInA = StartTM.TransformPosition(ClosestB);
				const T InGJKPreDist = FMath::Sqrt(InGJKPreDist2);
				OutNormal = (ClosestBInA - ClosestA).GetUnsafeNormal();	//question: should we just use InGJKPreDist2?
				const T Penetration = ThicknessA + ThicknessB - InGJKPreDist;
				const TVector<T, 3> ClosestLocal = ClosestB - OutNormal * ThicknessB;

				OutPosition = StartPoint + ClosestLocal + OutNormal * Penetration;
				OutTime = -Penetration;
			}
			else
			{
				//todo: use EPA
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
						const TVector<T, 3> SupportBLocal = B.Support2(DirInB);
						return StartTM.TransformPositionNoScale(SupportBLocal);
					};

					T Penetration;
					TVec3<T> MTD, ClosestA, ClosestBInA;
					if (EPA(VertsA, VertsB, SupportAFunc, SupportBAtOriginFunc, Penetration, MTD, ClosestA, ClosestBInA) != EPAResult::BadInitialSimplex)
					{
						OutNormal = MTD;
						OutTime = -Penetration - Inflation;
						OutPosition = ClosestA;
					}
					else
					{
						//assume touching hit
						OutTime = -Inflation;
						OutNormal = { 0,0,1 };
						OutPosition = As[0] + OutNormal * ThicknessA;
					}
				}
				else
				{
					//didn't even go into gjk loop, touching hit
					OutTime = -Inflation;
					OutNormal = { 0,0,1 };
					OutPosition = As[0] + OutNormal * ThicknessA;
				}
			}
		}

		return true;
	}

	/**
	 * Used by GJKDistance. It must return a vector in the Minkowski sum A - B. In principle this can be the vector of any point
	 * in A to any point in B, but some choices will cause GJK to minimize faster (e.g., for two spheres, we can easily calculate
	 * the actual separating vector and GJK will converge immediately).
	 */
	template <typename T, typename TGeometryA, typename TGeometryB>
	TVector<T, 3> GJKDistanceInitialV(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM)
	{
		const TVec3<T> V(1, 0, 0);
		const TVector<T, 3> SupportA = A.Support(-V, 0);
		const TVector<T, 3> VInB = BToATM.GetRotation().Inverse() * V;
		const TVector<T, 3> SupportBLocal = B.Support(VInB, 0);
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
		T DeltaLen = Delta.Size();
		T RadiusAB = A.GetRadius() + B.GetRadius();
		if (DeltaLen > RadiusAB)
		{
			return Delta -  Delta * (RadiusAB / DeltaLen);
		}
		return TVector<T, 3>(0, 0, 0);
	}

	// Overloads for geometry types which don't have centroids.
	template <typename T, typename TGeometryB>
	TVector<T, 3> GJKDistanceInitialV(const FImplicitObject& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM)
	{
		return -BToATM.GetTranslation();
	}

	template <typename T, typename TGeometryA>
	TVector<T, 3> GJKDistanceInitialV(TGeometryA A, const FImplicitObject& B, const TRigidTransform<T, 3>& BToATM)
	{
		return -BToATM.GetTranslation();
	}

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
	 * @return true if we succeeded in calculating the distance, false otherwise (i.e., false if objects are overlapping).
	 */
	template <typename T, typename TGeometryA, typename TGeometryB>
	bool GJKDistance(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM, T& OutDistance, TVector<T, 3>& OutNearestA, TVector<T, 3>& OutNearestB, const T Epsilon = (T)1e-6, const int32 MaxIts = 16)
	{
		check(A.IsConvex() && B.IsConvex());

		FSimplex SimplexIDs;
		TVector<T, 3> Simplex[4], SimplexA[4], SimplexB[4];
		T Barycentric[4] = { -1, -1, -1, -1 };

		const TRotation<T, 3> AToBRotation = BToATM.GetRotation().Inverse();
		T Mu = 0;

		// Select an initial vector in A - B
		TVector<T, 3> V = GJKDistanceInitialV(A, B, BToATM);
		T VLen = V.Size();

		int32 It = 0;
		while (VLen > Epsilon)
		{
			// Find a new point in A-B that is closer to the origin
			// NOTE: we do not use support thickness here. Thickness is used when separating objects
			// so that GJK can find a solution, but that can be added in a later step.
			const TVector<T, 3> SupportA = A.Support(-V, 0);
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.Support(VInB, 0);
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
				OutDistance = VLen;
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
				return true;
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

		// Our geometries overlap - we did not produce the near points (and didn't set distance, which is zero)
		return false;
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
	bool GJKPenetrationTemp(const TGeometryA& A, const TCapsule<T>& B, const TRigidTransform<T, 3>& BToATM, TVector<T, 3>& OutPositionA, TVector<T, 3>& OutPositionB, TVector<T, 3>& OutNormal, T& OutDistance, const T ThicknessA = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T ThicknessB = 0, const T Epsilon = (T)1e-6, const int32 MaxIts = 16)
	{
		float SegmentDistance;
		const TSegment<T>& Segment = B.GetSegment();
		const float MarginB = B.GetRadius();
		TVector<float, 3> PositionBInB;
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
