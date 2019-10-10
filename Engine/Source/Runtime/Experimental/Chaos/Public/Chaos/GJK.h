// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	template <typename T, int d>
	TVector<T, d> LineSimplexFindOrigin(const TVector<T, d>* Simplex, int32* Idxs, int32& NumVerts, T* OutBarycentric)
	{
		const TVector<T, d>& X0 = Simplex[Idxs[0]];
		const TVector<T, d>& X1 = Simplex[Idxs[1]];
		const TVector<T, d> X0ToX1 = X1 - X0;

		//Closest Point = (-X0 dot X1-X0) / ||(X1-X0)||^2 * (X1-X0)

		const TVector<T, d> OriginToX0 = -X0;
		const T Dot = TVector<T, d>::DotProduct(OriginToX0, X0ToX1);

		if (Dot <= 0)
		{
			NumVerts = 1;
			OutBarycentric[Idxs[0]] = 1;
			return X0;
		}

		const T X0ToX1Squared = X0ToX1.SizeSquared();

		if (X0ToX1Squared < Dot || X0ToX1Squared <= std::numeric_limits<float>::min())	//if dividing gives 1+ or the line is degenerate
		{
			NumVerts = 1;
			Idxs[0] = Idxs[1];
			OutBarycentric[Idxs[1]] = 1;
			return X1;
		}

		const T Ratio = Dot / X0ToX1Squared;
		const TVector<T, d> Closest = Ratio * (X0ToX1)+X0;	//note: this could pass X1 by machine epsilon, but doesn't seem worth it for now
		OutBarycentric[Idxs[0]] = 1 - Ratio;
		OutBarycentric[Idxs[1]] = Ratio;
		return Closest;
	}

	struct FSimplex
	{
		int32 NumVerts;
		int32 Idxs[4];
		int32 operator[](int32 Idx) const { return Idxs[Idx]; }
		int32& operator[](int32 Idx) { return Idxs[Idx]; }

		FSimplex& operator=(const FSimplex& Other)
		{
			NumVerts = Other.NumVerts;
			for (int i = 0; i < 4; ++i)
			{
				Idxs[i] = Other.Idxs[i];
			}
			return *this;
		}

		FSimplex(std::initializer_list<int32> InIdxs = {})
			: NumVerts(InIdxs.size())
		{
			check(NumVerts <= 4);
			int32 i = 0;
			for (int32 Idx : InIdxs)
			{
				Idxs[i++] = Idx;
			}
			while (i < 4)
			{
				Idxs[i++] = 0;	//some code uses these for lookup regardless of NumVerts. Makes for faster code so just use 0 to make lookup safe
			}
		}
	};

	template <typename T>
	bool SignMatch(T A, T B)
	{
		return (A > 0 && B > 0) || (A < 0 && B < 0);
	}

	template <typename T>
	TVector<T, 3> TriangleSimplexFindOrigin(const TVector<T, 3>* Simplex, FSimplex& Idxs, T* OutBarycentric)
	{
		/* Project the origin onto the triangle plane:
		   Let n = (b-a) cross (c-a)
		   Let the distance from the origin dist = ((-a) dot n / ||n||^2)
		   Then the projection p = 0 - dist * n = (a dot n) / ||n||^2
		*/

		const int32 Idx0 = Idxs[0];
		const int32 Idx1 = Idxs[1];
		const int32 Idx2 = Idxs[2];

		const TVector<T, 3>& X0 = Simplex[Idx0];
		const TVector<T, 3>& X1 = Simplex[Idx1];
		const TVector<T, 3>& X2 = Simplex[Idx2];

		const TVector<T, 3> X0ToX1 = X1 - X0;
		const TVector<T, 3> X0ToX2 = X2 - X0;
		const TVector<T, 3> TriNormal = TVector<T, 3>::CrossProduct(X0ToX1, X0ToX2);

		/*
		   We want |(a dot n) / ||n||^2| < 1 / eps to avoid inf. But note that |a dot n| <= ||a||||n|| and so
		   |(a dot n) / ||n||^2| <= ||a|| / ||n| < 1 / eps requires that ||eps*a||^2 < ||n||^2
		*/
		const T TriNormal2 = TriNormal.SizeSquared();
		const T Eps2 = (X0*std::numeric_limits<T>::min()).SizeSquared();
		if (Eps2 >= TriNormal2)	//equality fixes case where both X0 and TriNormal2 are 0
		{
			//degenerate triangle so return previous line results
			Idxs.NumVerts = 2;
			return LineSimplexFindOrigin(Simplex, Idxs.Idxs, Idxs.NumVerts, OutBarycentric);
		}

		const TVector<T, 3> TriNormalOverSize2 = TriNormal / TriNormal2;
		const T SignedDistance = TVector<T, 3>::DotProduct(X0, TriNormalOverSize2);
		const TVector<T, 3> ProjectedOrigin = TriNormal * SignedDistance;

		/*
			Let p be the origin projected onto the triangle plane. We can represent the point p in a 2d subspace spanned by the triangle
			|a_u, b_u, c_u| |lambda_1| = |p_u|
			|a_v, b_v, c_v| |lambda_2| = |p_v|
			|1,   1,   1  | |lambda_3| = |1  |

			Cramer's rule gives: lambda_i = det(M_i) / det(M)
			To choose u and v we simply test x,y,z to see if any of them are linearly independent
		*/

		T DetM = 0;	//not needed but fixes compiler warning
		int32 BestAxisU = INDEX_NONE;
		int32 BestAxisV = 0;	//once best axis is chosen use the axes that go with it in the right order

		{
			T MaxAbsDetM = 0;	//not needed but fixes compiler warning
			int32 AxisU = 1;
			int32 AxisV = 2;
			for (int32 CurAxis = 0; CurAxis < 3; ++CurAxis)
			{
				T TmpDetM = X1[AxisU] * X2[AxisV] - X2[AxisU] * X1[AxisV]
					+ X2[AxisU] * X0[AxisV] - X0[AxisU] * X2[AxisV]
					+ X0[AxisU] * X1[AxisV] - X1[AxisU] * X0[AxisV];
				const T AbsDetM = FMath::Abs(TmpDetM);
				if (BestAxisU == INDEX_NONE || AbsDetM > MaxAbsDetM)
				{
					MaxAbsDetM = AbsDetM;
					DetM = TmpDetM;
					BestAxisU = AxisU;
					BestAxisV = AxisV;
				}
				AxisU = AxisV;
				AxisV = CurAxis;
			}
		}

		/*
			Now solve for the cofactors (i.e. the projected origin replaces the column of each cofactor).
			Notice that this is really the area of each sub triangle with the projected origin.
			If the sign of the determinants is different than the sign of the entire triangle determinant then we are outside of the triangle.
			The conflicting signs indicate which voronoi regions to search

			Cofactor_a =    |p_u b_u c_u|  Cofactor_b =    |a_u p_u c_u|  Cofactor_c = |a_u b_u p_u|
						 det|p_v b_v c_v|               det|a_v p_v c_v|            det|a_v c_v p_v|
							|1   1  1   |                  |1   1  1   |               |1   1  1   |
		*/
		const TVector<T, 3>& P0 = ProjectedOrigin;
		const TVector<T, 3> P0ToX0 = X0 - P0;
		const TVector<T, 3> P0ToX1 = X1 - P0;
		const TVector<T, 3> P0ToX2 = X2 - P0;

		const T Cofactors[3] = {
			P0ToX1[BestAxisU] * P0ToX2[BestAxisV] - P0ToX2[BestAxisU] * P0ToX1[BestAxisV],
			-P0ToX0[BestAxisU] * P0ToX2[BestAxisV] + P0ToX2[BestAxisU] * P0ToX0[BestAxisV],
			P0ToX0[BestAxisU] * P0ToX1[BestAxisV] - P0ToX1[BestAxisU] * P0ToX0[BestAxisV]
		};

		bool bSignMatch[3];
		FSimplex SubSimplices[3] = { {Idx1,Idx2}, {Idx0,Idx2}, {Idx0,Idx1} };
		TVector<T, 3> ClosestPointSub[3];
		T SubBarycentric[3][3];
		int32 ClosestSubIdx = INDEX_NONE;
		T MinSubDist2 = 0;	//not needed
		bool bInside = true;
		for (int32 Idx = 0; Idx < 3; ++Idx)
		{
			bSignMatch[Idx] = SignMatch(DetM, Cofactors[Idx]);
			if (!bSignMatch[Idx])
			{
				bInside = false;
				ClosestPointSub[Idx] = LineSimplexFindOrigin(Simplex, SubSimplices[Idx].Idxs, SubSimplices[Idx].NumVerts, SubBarycentric[Idx]);

				const T Dist2 = ClosestPointSub[Idx].SizeSquared();
				if (ClosestSubIdx == INDEX_NONE || Dist2 < MinSubDist2)
				{
					MinSubDist2 = Dist2;
					ClosestSubIdx = Idx;
				}
			}
		}

		if (bInside)
		{
			//SignMatch ensures that DetM is not 0. The Det_i / Det_m ratio is always between 0-1 because it represents the ratio of areas and Det_m is the total area
			const T InvDetM = 1 / DetM;
			T Lambda0 = Cofactors[0] * InvDetM;
			T Lambda1 = Cofactors[1] * InvDetM;
			//T Lambda2 = 1 - Lambda1 - Lambda0;
			T Lambda2 = Cofactors[2] * InvDetM;
			const TVector<T, 3> ClosestPoint = X0 * Lambda0 + X1 * Lambda1 + X2 * Lambda2;	//could be slightly outside if |lambda1| < 1e-7 or |lambda2| < 1e-7. Should we clamp?
			OutBarycentric[Idx0] = Lambda0;
			OutBarycentric[Idx1] = Lambda1;
			OutBarycentric[Idx2] = Lambda2;
			return ClosestPoint;
		}
		else
		{
			Idxs = SubSimplices[ClosestSubIdx];
			OutBarycentric[Idx0] = SubBarycentric[ClosestSubIdx][Idx0];
			OutBarycentric[Idx1] = SubBarycentric[ClosestSubIdx][Idx1];
			OutBarycentric[Idx2] = SubBarycentric[ClosestSubIdx][Idx2];
			return ClosestPointSub[ClosestSubIdx];
		}
	}

	template <typename T>
	TVector<T, 3> TetrahedronSimplexFindOrigin(const TVector<T, 3>* Simplex, FSimplex& Idxs, T* OutBarycentric)
	{
		const int32 Idx0 = Idxs[0];
		const int32 Idx1 = Idxs[1];
		const int32 Idx2 = Idxs[2];
		const int32 Idx3 = Idxs[3];

		const TVector<T, 3>& X0 = Simplex[Idx0];
		const TVector<T, 3>& X1 = Simplex[Idx1];
		const TVector<T, 3>& X2 = Simplex[Idx2];
		const TVector<T, 3>& X3 = Simplex[Idx3];

		//Use signed volumes to determine if origin is inside or outside
		/*
			M = [X0x X1x X2x X3x;
				 X0y X1y X2y X3y;
				 X0z X1z X2z X3z;
				 1   1   1   1]
		*/

		T Cofactors[4];
		Cofactors[0] = -TVector<T, 3>::DotProduct(X1, TVector<T, 3>::CrossProduct(X2, X3));
		Cofactors[1] = TVector<T, 3>::DotProduct(X0, TVector<T, 3>::CrossProduct(X2, X3));
		Cofactors[2] = -TVector<T, 3>::DotProduct(X0, TVector<T, 3>::CrossProduct(X1, X3));
		Cofactors[3] = TVector<T, 3>::DotProduct(X0, TVector<T, 3>::CrossProduct(X1, X2));
		T DetM = (Cofactors[0] + Cofactors[1]) + (Cofactors[2] + Cofactors[3]);

		bool bSignMatch[4];
		FSimplex SubIdxs[4] = { {1,2,3}, {0,2,3}, {0,1,3}, {0,1,2} };
		TVector<T, 3> ClosestPointSub[4];
		T SubBarycentric[4][4];
		int32 ClosestTriangleIdx = INDEX_NONE;
		T MinTriangleDist2 = 0;

		bool bInside = true;
		for (int Idx = 0; Idx < 4; ++Idx)
		{
			bSignMatch[Idx] = SignMatch(DetM, Cofactors[Idx]);
			if (!bSignMatch[Idx])
			{
				bInside = false;
				ClosestPointSub[Idx] = TriangleSimplexFindOrigin(Simplex, SubIdxs[Idx], SubBarycentric[Idx]);

				const T Dist2 = ClosestPointSub[Idx].SizeSquared();
				if (ClosestTriangleIdx == INDEX_NONE || Dist2 < MinTriangleDist2)
				{
					MinTriangleDist2 = Dist2;
					ClosestTriangleIdx = Idx;
				}
			}
		}

		if (bInside)
		{
			OutBarycentric[Idx0] = Cofactors[0] / DetM;
			OutBarycentric[Idx1] = Cofactors[1] / DetM;
			OutBarycentric[Idx2] = Cofactors[2] / DetM;
			OutBarycentric[Idx3] = Cofactors[3] / DetM;

			return TVector<T, 3>(0);
		}

		Idxs = SubIdxs[ClosestTriangleIdx];

		OutBarycentric[Idx0] = SubBarycentric[ClosestTriangleIdx][Idx0];
		OutBarycentric[Idx1] = SubBarycentric[ClosestTriangleIdx][Idx1];
		OutBarycentric[Idx2] = SubBarycentric[ClosestTriangleIdx][Idx2];
		OutBarycentric[Idx3] = SubBarycentric[ClosestTriangleIdx][Idx3];

		return ClosestPointSub[ClosestTriangleIdx];
	}

	template <typename T>
	void ReorderGJKArray(T* Data, FSimplex& Idxs)
	{
		const T D0 = Data[Idxs[0]];
		const T D1 = Data[Idxs[1]];
		const T D2 = Data[Idxs[2]];
		const T D3 = Data[Idxs[3]];
		Data[0] = D0;
		Data[1] = D1;
		Data[2] = D2;
		Data[3] = D3;
	}

	template <typename T>
	TVector<T, 3> SimplexFindClosestToOrigin(TVector<T, 3>* Simplex, FSimplex& Idxs, T* OutBarycentric, TVector<T, 3>* A = nullptr, TVector<T, 3>* B = nullptr)
	{
		TVector<T, 3> ClosestPoint;
		switch (Idxs.NumVerts)
		{
		case 1:
			OutBarycentric[0] = 1;
			ClosestPoint = Simplex[Idxs[0]]; break;
		case 2:
		{
			ClosestPoint = LineSimplexFindOrigin(Simplex, Idxs.Idxs, Idxs.NumVerts, OutBarycentric);
			break;
		}
		case 3:
		{
			ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, OutBarycentric);
			break;
		}
		case 4:
		{
			ClosestPoint = TetrahedronSimplexFindOrigin(Simplex, Idxs, OutBarycentric);
			break;
		}
		default:
			check(false);
			ClosestPoint = TVector<T, 3>(0);
		}

		ReorderGJKArray(Simplex, Idxs);
		ReorderGJKArray(OutBarycentric, Idxs);
		if (A)
		{
			ReorderGJKArray(A, Idxs);
		}

		if (B)
		{
			ReorderGJKArray(B, Idxs);
		}

		Idxs[0] = 0;
		Idxs[1] = 1;
		Idxs[2] = 2;
		Idxs[3] = 3;

		return ClosestPoint;
	}

	/** Determines if two convex geometries overlap.
	 @A The first geometry
	 @B The second geometry
	 @BToATM The transform of B in A's local space
	 @ThicknessA The amount of geometry inflation for Geometry A(for example if the surface distance of two geometries with thickness 0 would be 2, a thickness of 0.5 would give a distance of 1.5)
	 @InitialDir The first direction we use to search the CSO
	 @ThicknessB The amount of geometry inflation for Geometry B(for example if the surface distance of two geometries with thickness 0 would be 2, a thickness of 0.5 would give a distance of 1.5)
	 @return True if the geometries overlap, False otherwise */

	template <typename T, typename TGeometryA, typename TGeometryB>
	bool GJKIntersection(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM, const T ThicknessA = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0), const T ThicknessB = 0)
	{
		check(A.IsConvex() && B.IsConvex());
		TVector<T, 3> V = -InitialDir;
		FSimplex SimplexIDs;
		TVector<T, 3> Simplex[4];
		T Barycentric[4];
		const TRotation<T, 3> AToBRotation = BToATM.GetRotation().Inverse();
		bool bTerminate;
		bool bNearZero = false;
		int NumIterations = 0;
		T PrevDist2 = FLT_MAX;
		do
		{
			if (!ensure(NumIterations++ < 32))	//todo: take this out
			{
				break;	//if taking too long just stop. This should never happen
			}
			const TVector<T, 3> NegV = -V;
			const TVector<T, 3> SupportA = A.Support(NegV, ThicknessA);	//todo: add thickness to quadratic geometry to avoid quadratic vs quadratic when possible
			const TVector<T, 3> VInB = AToBRotation * V;
			const TVector<T, 3> SupportBLocal = B.Support(VInB, ThicknessB);
			const TVector<T, 3> SupportB = BToATM.TransformPositionNoScale(SupportBLocal);
			const TVector<T, 3> W = SupportA - SupportB;

			if (TVector<T, 3>::DotProduct(V, W) > 0)
			{
				return false;
			}

			SimplexIDs[SimplexIDs.NumVerts] = SimplexIDs.NumVerts;
			Simplex[SimplexIDs.NumVerts++] = W;

			V = SimplexFindClosestToOrigin(Simplex, SimplexIDs, Barycentric);

			T NewDist2 = V.SizeSquared();
			bNearZero = NewDist2 < 1e-6;

			//as simplices become degenerate we will stop making progress. This is a side-effect of precision, in that case take V as the current best approximation
			//question: should we take previous v in case it's better?
			const bool bMadeProgress = NewDist2 < PrevDist2;
			bTerminate = bNearZero || !bMadeProgress;

			PrevDist2 = NewDist2;

		} while (!bTerminate);

		return bNearZero;
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

		T Barycentric[4];

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

	/**
	 * Used by GJKDistance. It must return a vector in the Minkowski sum A - B. In principle this can be the vector of any point
	 * in A to any point in B, but some choices will cause GJK to minimize faster (e.g., for two spheres, we can easily calculate
	 * the actual separating vector and GJK will converge immediately).
	 */
	template <typename T, typename TGeometryA, typename TGeometryB>
	TVector<T, 3> GJKDistanceInitialV(const TGeometryA& A, const TGeometryB& B, const TRigidTransform<T, 3>& BToATM)
	{
		return A.GetCenter() - (B.GetCenter() + BToATM.GetTranslation());
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

	/** Approximate the collision point using Core Shape Reduction.
	 @A The first geometry
	 @ATM The transform of the first geometry
	 @B The second geometry
	 @BTM The transform of the second geometry
	 @OutLocation The collision point.
	 @OutPhi The depth along the normal. (FLT_MAX if failed)
	 @OutNormal The impact normal (in A's world space)
	 */
	template<typename T, int d, typename TGeometryA, typename TGeometryB>
	void GJKCoreShapeIntersection(const TGeometryA& A, const TRigidTransform<T, d>& ATM, const TGeometryB& B, const TRigidTransform<T, d>& BTM,
		TVector<T, d> & OutLocation, T & OutPhi, TVector<T, d> & OutNormal, const T Thickness = 0, const TVector<T, 3>& InitialDir = TVector<T, 3>(1, 0, 0))
	{
		OutPhi = FLT_MAX;
		check(A.IsConvex() && B.IsConvex());
		TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);
		TVector<T, d> ABVector = -BToATM.GetTranslation();

		//
		// Guess an initial thickness that will separate the bodies, then iterate until they are separated
		//
		T MinBoxMin = FMath::Min(A.BoundingBox().Extents().Min(), B.BoundingBox().Extents().Min());
		T MinLocalThickness = -MinBoxMin;
		T Delta = MinBoxMin / 20.; // reduce by delta at each step
		T LocalThickness = 0.0;
		while (GJKIntersection<T>(A, B, BToATM, LocalThickness, InitialDir, LocalThickness))
		{
			LocalThickness -= Delta;
			if (LocalThickness < MinLocalThickness)
			{
				return;
			}
		}
		LocalThickness *= 1.05; // corner case when equal.

		//
		// Measure the separation distance from a raycast, then remove the LocalThickness.
		//
		auto Calculate = [&](const TVector<T, d>& InRay, TVector<T, d> & Location, T & Phi, TVector<T, d> & Normal)
		{
			T RayTime = FLT_MAX;
			TVector<T, d> RayPosition, RayNormal, InRayNormal = InRay.GetSafeNormal();
			if (GJKRaycast(A, B, BToATM, InRayNormal, (T)InRay.Size(), RayTime, RayPosition, RayNormal, LocalThickness, InitialDir, LocalThickness))
			{
				// Note: Initial overlaps in GJKRaycast cannot set a contact position/normal. It sets RayTime = 0 in this case.
				if (RayTime > 0)
				{
					Location = ATM.TransformPosition(RayPosition + RayNormal * (-LocalThickness));
					Normal = -ATM.TransformVector(RayNormal);

					// @todo: Remove PhiWithNormal call
					TVector<T, d> TmpNormal;
					Phi = B.PhiWithNormal(BTM.InverseTransformPosition(Location), TmpNormal);
					return true;
				}
			}
			return false;
		};

		Calculate(ABVector, OutLocation, OutPhi, OutNormal);
	}
}
