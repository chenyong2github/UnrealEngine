// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestGJK.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Chaos/GJK.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/ImplicitObjectScaled.h"

namespace ChaosTest
{
	using namespace Chaos;

	//for each simplex test:
	//- points get removed
	// - points off simplex return false
	//- points in simplex return true
	//- degenerate simplex

	template <typename T>
	void SimplexLine()
	{
		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1}, {-1,-1,1} };
			int32 Idxs[] = { 0,1 };
			int32 NumVerts = 2;
			const TVector<T,3> ClosestPoint = LineSimplexFindOrigin(Simplex, Idxs, NumVerts, Barycentric);
			EXPECT_EQ(NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
		}

		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1}, {1,1,1} };
			int32 Idxs[] = { 0,1 };
			int32 NumVerts = 2;
			const TVector<T, 3> ClosestPoint = LineSimplexFindOrigin(Simplex, Idxs, NumVerts, Barycentric);
			EXPECT_EQ(NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
		}

		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {1,1,1}, {1,2,3} };
			int32 Idxs[] = { 0,1 };
			int32 NumVerts = 2;
			const TVector<T, 3> ClosestPoint = LineSimplexFindOrigin(Simplex, Idxs, NumVerts, Barycentric);
			EXPECT_EQ(NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 1);
			EXPECT_FLOAT_EQ(Barycentric[0], 1);
			EXPECT_EQ(Idxs[0], 0);
		}

		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {10,11,12}, {1,2,3} };
			int32 Idxs[] = { 0,1 };
			int32 NumVerts = 2;
			const TVector<T, 3> ClosestPoint = LineSimplexFindOrigin(Simplex, Idxs, NumVerts, Barycentric);
			EXPECT_EQ(NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 2);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 3);
			EXPECT_FLOAT_EQ(Barycentric[1], 1);
			EXPECT_EQ(Idxs[0], 1);
		}

		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {1,1,1}, {1,1,1} };
			int32 Idxs[] = { 0,1 };
			int32 NumVerts = 2;
			const TVector<T, 3> ClosestPoint = LineSimplexFindOrigin(Simplex, Idxs, NumVerts, Barycentric);
			EXPECT_EQ(NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 1);
			EXPECT_FLOAT_EQ(Barycentric[0], 1);
			EXPECT_EQ(Idxs[0], 0);
		}

		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {1,-1e-16,1}, {1,1e-16,1} };
			int32 Idxs[] = { 0,1 };
			int32 NumVerts = 2;
			const TVector<T, 3> ClosestPoint = LineSimplexFindOrigin(Simplex, Idxs, NumVerts, Barycentric);
			EXPECT_EQ(NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 1);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
		}
	}

	template <typename T>
	void SimplexTriangle()
	{
		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1}, {-1,1,-1}, {-2,1,-1} };
			FSimplex Idxs = { 0,1, 2 };
			
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], -1);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
		}

		{
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1},{-2,1,-1}, {-1,1,-1} };
			FSimplex Idxs = { 0,1, 2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], -1);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 2);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[2], 0.5);
		}

		{
			//corner
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {1,1,1},{2,1,1}, {2,2,1} };
			FSimplex Idxs = { 1,0, 2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 1);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_FLOAT_EQ(Barycentric[0], 1);
		}

		{
			//corner equal
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {0,0,0},{2,1,1}, {2,2,1} };
			FSimplex Idxs = { 0,1, 2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_FLOAT_EQ(Barycentric[0], 1);
		}

		{
			//edge equal
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,0,0},{1,0,0}, {0,2,0} };
			FSimplex Idxs = { 2,0, 1 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
		}

		{
			//triangle equal
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,0,-1},{1,0,-1}, {0,0,1} };
			FSimplex Idxs = { 0,1, 2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 3);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_EQ(Idxs[2], 2);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[2], 0.5);
		}

		{
			//co-linear
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1},{-1,1,-1}, {-1,1.2,-1} };
			FSimplex Idxs = { 0,1, 2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], -1);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);	//degenerate triangle throws out newest point
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
		}

		{
			//single point
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1},{-1,-1,-1}, {-1,-1,-1} };
			FSimplex Idxs = { 0,2, 1 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], -1);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_FLOAT_EQ(Barycentric[0], 1);
		}

		{
			//corner perfect split
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,0},{1,-1,0}, {0,-0.5,0} };
			FSimplex Idxs = { 0,2, 1 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], -0.5);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 2);
			EXPECT_FLOAT_EQ(Barycentric[2], 1);
		}

		{
			//triangle face correct distance
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1},{1,-1,-1}, {0,1,-1} };
			FSimplex Idxs = { 0,1,2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 3);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], -1);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_EQ(Idxs[2], 2);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[2], 0.5);
		}

		{
			//tiny triangle middle point
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1e-9,-1e-9,-1e-9},{-1e-9,1e-9,-1e-9}, {-1e-9,0,1e-9} };
			FSimplex Idxs = { 0,1,2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 3);
			EXPECT_FLOAT_EQ(ClosestPoint[0], -1e-9);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_EQ(Idxs[2], 2);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[2], 0.5);
		}

		{
			//non cartesian triangle plane
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {2, 0, -1}, {0, 2, -1}, {1, 1, 1} };
			FSimplex Idxs = { 0,1,2 };
			const TVector<T, 3> ClosestPoint = TriangleSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 3);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_EQ(Idxs[2], 2);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.25);
			EXPECT_FLOAT_EQ(Barycentric[2], 0.5);
		}
	}

	template <typename T>
	void SimplexTetrahedron()
	{
		{
			//top corner
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1}, {1,-1,-1}, {0,1,-1}, {0,0,-0.5} };
			FSimplex Idxs = { 0,1,2,3 };
			const TVector<T, 3> ClosestPoint = TetrahedronSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 1);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], -0.5);
			EXPECT_EQ(Idxs[0], 3);
			EXPECT_FLOAT_EQ(Barycentric[3], 1);
		}

		{
			//inside
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,-1}, {1,-1,-1}, {0,1,-1}, {0,0,0.5} };
			FSimplex Idxs = { 0,1,2,3 };
			const TVector<T, 3> ClosestPoint = TetrahedronSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 4);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_EQ(Idxs[2], 2);
			EXPECT_EQ(Idxs[3], 3);
			EXPECT_FLOAT_EQ(Barycentric[0] + Barycentric[1] + Barycentric[2] + Barycentric[3], 1);
		}

		{
			//face
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {0,0,-1.5}, {-1,-1,-1}, {1,-1,-1}, {0,1,-1} };
			FSimplex Idxs = { 0,1,2,3 }; 
			const TVector<T, 3> ClosestPoint = TetrahedronSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 3);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[2], -1);
			EXPECT_EQ(Idxs[0], 1);
			EXPECT_EQ(Idxs[1], 2);
			EXPECT_EQ(Idxs[2], 3);
			EXPECT_FLOAT_EQ(Barycentric[1] + Barycentric[2] + Barycentric[3], 1);
		}

		{
			//edge
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,0}, {1,-1,0}, {0,-1,-1}, {0, -2, -1} };
			FSimplex Idxs = { 0,1,2,3 };
			const TVector<T, 3> ClosestPoint = TetrahedronSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
		}

		{
			//degenerate
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-1,-1,0}, {1,-1,0}, {0,-1,-1}, {0, -1, -0.5} };
			FSimplex Idxs = { 0,1,2,3 };
			const TVector<T, 3> ClosestPoint = TetrahedronSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 2);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_FLOAT_EQ(Barycentric[0], 0.5);
			EXPECT_FLOAT_EQ(Barycentric[1], 0.5);
		}

		{
			//wide angle, bad implementation would return edge but it's really a face
			T Barycentric[4];
			const TVector<T, 3> Simplex[] = { {-10000,-1,10000}, {1,-1,10000}, {4,-3,10000}, {1, -1, -10000} };
			FSimplex Idxs = { 0,1,2,3 };
			const TVector<T, 3> ClosestPoint = TetrahedronSimplexFindOrigin(Simplex, Idxs, Barycentric);
			EXPECT_EQ(Idxs.NumVerts, 3);
			EXPECT_FLOAT_EQ(ClosestPoint[0], 0);
			EXPECT_FLOAT_EQ(ClosestPoint[1], -1);
			EXPECT_FLOAT_EQ(ClosestPoint[2], 0);
			EXPECT_EQ(Idxs[0], 0);
			EXPECT_EQ(Idxs[1], 1);
			EXPECT_EQ(Idxs[2], 3);
			EXPECT_FLOAT_EQ(Barycentric[0] + Barycentric[1] + Barycentric[3], 1);
		}
	}

	//For each gjk test we should test:
	// - thickness
	// - transformed geometry
	// - rotated geometry
	// - degenerate cases
	// - near miss, near hit
	// - multiple initial dir

	template <typename T>
	void GJKSphereSphereTest()
	{
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);
		TSphere<T, 3> B(TVector<T, 3>(4, 0, 0), 2);

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>::Identity, 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(-1.1, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//hit from thickness
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(-1.1, 0, 0), TRotation<T, 3>::Identity), 0.105, InitialDir));

			//miss with thickness
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(-1.1, 0, 0), TRotation<T, 3>::Identity), 0.095, InitialDir));

			//hit with rotation
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(6.5, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 1, InitialDir));

			//miss with rotation
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(6.5, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 0.01, InitialDir));

			//hit tiny
			TSphere<T, 3> Tiny(TVector<T, 3>(0), 1e-2);
			EXPECT_TRUE(GJKIntersection<T>(A, Tiny, TRigidTransform<T, 3>(TVector<T, 3>(15, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//miss tiny
			EXPECT_FALSE(GJKIntersection<T>(A, Tiny, TRigidTransform<T, 3>(TVector<T, 3>(15 + 1e-1, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));
		}
	}


	template <typename T>
	void GJKSphereBoxTest()
	{
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);
		TAABB<T, 3> B(TVector<T, 3>(-4, -2, -4), TVector<T,3>(4,2,4));

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0.9, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//rotate and hit
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.1, 0, 0), TRotation<T, 3>::FromVector(TVector<T,3>(0,0,PI*0.5))), 0, InitialDir));

			//rotate and miss
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2.9, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0, InitialDir));

			//rotate and hit from thickness
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2.9, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0.1, InitialDir));

			//hit thin
			TAABB<T, 3> Thin(TVector<T, 3>(4, -2, -4), TVector<T, 3>(4, 2, 4));
			EXPECT_TRUE(GJKIntersection<T>(A, Thin, TRigidTransform<T, 3>(TVector<T, 3>(1+1e-2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, Thin, TRigidTransform<T, 3>(TVector<T, 3>(1 - 1e-2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//hit line
			TAABB<T, 3> Line(TVector<T, 3>(4, -2, 0), TVector<T, 3>(4, 2, 0));
			EXPECT_TRUE(GJKIntersection<T>(A, Line, TRigidTransform<T, 3>(TVector<T, 3>(1 + 1e-2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, Line, TRigidTransform<T, 3>(TVector<T, 3>(1 - 1e-2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));
		}
	}


	template <typename T>
	void GJKSphereCapsuleTest()
	{
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);
		TCapsule<T> B(TVector<T, 3>(0, 0, -3), TVector<T, 3>(0, 0, 3), 3);

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2-1e-2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//thickness
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), 1.01, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), 0.99, InitialDir));

			//rotation hit
			EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(-1+1e-2, 0, 0), TRotation<T, 3>::FromVector(TVector<T,3>(0,PI*0.5,0))), 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(-1-1e-2, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, PI*0.5, 0))), 0, InitialDir));

			//degenerate
			TCapsule<T> Line(TVector<T, 3>(0, 0, -3), TVector<T, 3>(0, 0, 3), 0);
			EXPECT_TRUE(GJKIntersection<T>(A, Line, TRigidTransform<T, 3>(TVector<T, 3>(5+1e-2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, Line, TRigidTransform<T, 3>(TVector<T, 3>(5 - 1e-2, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));
		}
	}


	template <typename T>
	void GJKSphereConvexTest()
	{
		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);

		{
			//Tetrahedron
			TParticles<T, 3> HullParticles;
			HullParticles.AddParticles(4);
			HullParticles.X(0) = { -1,-1,-1 };
			HullParticles.X(1) = { 1,-1,-1 };
			HullParticles.X(2) = { 0,1,-1 };
			HullParticles.X(3) = { 0,0,1 };
			FConvex B(HullParticles);

			for (const TVector<T, 3>& InitialDir : InitialDirs)
			{
				//hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(5, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

				//near hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 + 1e-4, 1, 1), TRotation<T, 3>::Identity), 0, InitialDir));

				//near miss
				EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 - 1e-2, 1, 1), TRotation<T, 3>::Identity), 0, InitialDir));

				//rotated hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 + 1e-4, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0, InitialDir));

				//rotated miss
				EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 - 1e-2, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0, InitialDir));

				//rotated and inflated hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.5, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0.5 + 1e-4, InitialDir));

				//rotated and inflated miss
				EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.5, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0.5 - 1e-2, InitialDir));
			}
		}

		{
			//Triangle
			TParticles<T, 3> TriangleParticles;
			TriangleParticles.AddParticles(3);
			TriangleParticles.X(0) = { -1,-1,-1 };
			TriangleParticles.X(1) = { 1,-1,-1 };
			TriangleParticles.X(2) = { 0,1,-1 };
			FConvex B(TriangleParticles);

			//triangle
			for (const TVector<T, 3>& InitialDir : InitialDirs)
			{
				//hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(5, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

				//near hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 + 1e-2, 1, 1), TRotation<T, 3>::Identity), 0, InitialDir));

				//near miss
				EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 - 1e-2, 1, 1), TRotation<T, 3>::Identity), 0, InitialDir));

				//rotated hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 + 1e-2, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0, InitialDir));

				//rotated miss
				EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4 - 1e-2, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0, InitialDir));

				//rotated and inflated hit
				EXPECT_TRUE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.5, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0.5 + 1e-2, InitialDir));

				//rotated and inflated miss
				EXPECT_FALSE(GJKIntersection<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.5, 0, 1), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI*0.5))), 0.5 - 1e-2, InitialDir));
			}
		}
	}


	template <typename T>
	void GJKSphereScaledSphereTest()
	{
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);
		TUniquePtr<TSphere<T, 3>> Sphere = MakeUnique<TSphere<T, 3>>(TVector<T, 3>(4, 0, 0), 2);
		TImplicitObjectScaled<TSphere<T, 3>> Unscaled(MakeSerializable(Sphere), TVector<T,3>(1));
		TImplicitObjectScaled<TSphere<T, 3>> UniformScaled(MakeSerializable(Sphere), TVector<T, 3>(2));
		TImplicitObjectScaled<TSphere<T, 3>> NonUniformScaled(MakeSerializable(Sphere), TVector<T, 3>(2,1,1));

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			EXPECT_TRUE(GJKIntersection<T>(A, Unscaled, TRigidTransform<T, 3>::Identity, 0, InitialDir));
			EXPECT_TRUE(GJKIntersection<T>(A, UniformScaled, TRigidTransform<T, 3>::Identity, 0, InitialDir));
			//EXPECT_TRUE(GJKIntersection<T>(A, NonUniformScaled, TRigidTransform<T, 3>::Identity, 0, InitialDir));

			//miss
			EXPECT_FALSE(GJKIntersection<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(-1.1, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));
			EXPECT_FALSE(GJKIntersection<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(-7.1, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));
			//EXPECT_FALSE(GJKIntersection<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(-7.1, 0, 0), TRotation<T, 3>::Identity), 0, InitialDir));

			//hit from thickness
			EXPECT_TRUE(GJKIntersection<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(-1.1, 0, 0), TRotation<T, 3>::Identity), 0.105, InitialDir));
			EXPECT_TRUE(GJKIntersection<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(-7.1, 0, 0), TRotation<T, 3>::Identity), 0.105, InitialDir));
			//EXPECT_TRUE(GJKIntersection<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(-7.1, 0, 0), TRotation<T, 3>::Identity), 0.105, InitialDir));

			//miss with thickness
			EXPECT_FALSE(GJKIntersection<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(-1.1, 0, 0), TRotation<T, 3>::Identity), 0.095, InitialDir));
			EXPECT_FALSE(GJKIntersection<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(-7.1, 0, 0), TRotation<T, 3>::Identity), 0.095, InitialDir));
			//EXPECT_FALSE(GJKIntersection<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(-7.1, 0, 0), TRotation<T, 3>::Identity), 0.095, InitialDir));

			//hit with rotation
			EXPECT_TRUE(GJKIntersection<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(6.5, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 1, InitialDir));
			EXPECT_TRUE(GJKIntersection<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(8.1, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 1, InitialDir));
			//EXPECT_TRUE(GJKIntersection<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(8.1, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 1, InitialDir));

			//miss with rotation
			EXPECT_FALSE(GJKIntersection<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(6.5, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 0.01, InitialDir));
			EXPECT_FALSE(GJKIntersection<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(8.1, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 0.01, InitialDir));
			//EXPECT_FALSE(GJKIntersection<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(8.1, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))), 0.01, InitialDir));
		}
	}

	//For each gjkraycast test we should test:
	// - thickness
	// - initial overlap
	// - transformed geometry
	// - rotated geometry
	// - offset transform
	// - degenerate cases
	// - near miss, near hit
	// - multiple initial dir

	template <typename T>
	void GJKSphereSphereSweep()
	{
		typedef TVector<T, 3> TVector3;
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);
		TSphere<T, 3> B(TVector<T, 3>(1, 0, 0), 2);

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		constexpr T Eps = 1e-1;

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			T Time;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//hit offset
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T,3>(1,0,0), TRotation<T,3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 1, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//initial overlap
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(7, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, false, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);

			//MTD
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(7, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -5);
			EXPECT_VECTOR_NEAR(Position, TVector3(5,0,0), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			
			//EPA
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(9, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -7);	//perfect overlap, will default to 0,0,1 normal
			EXPECT_VECTOR_NEAR(Position, TVec3<T>(10,0,5), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(0, 0, 1), Eps);

			//miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//hit with thickness
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));

			//hit rotated
			const TRotation<T, 3> RotatedDown(TRotation<T, 3>::FromVector(TVector<T, 3>(0, PI * 0.5, 0)));
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//miss rotated
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 8.1), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//hit rotated with inflation
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));

			//near hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 - 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//near miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//degenerate
			TSphere<T, 3> Tiny(TVector<T, 3>(1, 0, 0), 1e-8);
			EXPECT_TRUE(GJKRaycast<T>(A, Tiny, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 8, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 4, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//right at end
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);

			// not far enough
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2 - 1e-2, Time, Position, Normal, 0, InitialDir));
		}
	}

	template <typename T>
	void GJKSphereBoxSweep()
	{
		typedef TVector<T, 3> TVector3;
		TAABB<T, 3> A(TVector<T, 3>(3, -1, 0), TVector<T, 3>(4, 1, 4));
		TSphere<T, 3> B(TVector<T, 3>(0, 0, 0), 1);

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		constexpr T Eps = 1e-1;

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			T Time;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(3, 0, 0), Eps);

			//hit offset
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1.5, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 0.5, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(3, 0, 0), Eps);

			//initial overlap
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4, 0, 4), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, false, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);

			//MTD without EPA
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4.25, 0, 2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -0.75);
			EXPECT_VECTOR_NEAR(Position, TVector3(4, 0, 2), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(1, 0, 0), Eps);

			//MTD with EPA
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4, 0, 2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -1);
			EXPECT_VECTOR_NEAR(Position, TVector3(4, 0, 2), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(1, 0, 0), Eps);

			//MTD with EPA
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.25, 0, 2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -1.25);
			EXPECT_VECTOR_NEAR(Position, TVector3(3, 0, 2), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);

			//MTD with EPA
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.4, 0, 3.75), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -1.25);
			EXPECT_VECTOR_NEAR(Position, TVector3(3.4, 0, 4), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(0, 0, 1), Eps);

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 6), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, -1).GetUnsafeNormal(), 4, Time, Position, Normal, 0, InitialDir));
			const T ExpectedTime = ((TVector<T, 3>(3, 0, 4) - TVector<T, 3>(1, 0, 6)).Size() - 1);
			EXPECT_NEAR(Time, ExpectedTime, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-sqrt(2) / 2, 0, sqrt(2) / 2), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(3, 0, 4), Eps);

			//near miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 5+1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));

			//near hit with inflation
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 5 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 2e-2, InitialDir));
			const T DistanceFromCorner = (Position - TVector<T, 3>(3, 0, 4)).Size();
			EXPECT_LT(DistanceFromCorner, 1e-1);

			//rotated box
			const TRotation<T, 3> Rotated(TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI * 0.5)));
			EXPECT_TRUE(GJKRaycast<T>(B, A, TRigidTransform<T, 3>(TVector<T, 3>(0), Rotated), TVector<T, 3>(0, -1, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(0, 1, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(0, 1, 0), Eps);

			//degenerate box
			TAABB<T, 3> Needle(TVector<T, 3>(3, 0, 0), TVector<T, 3>(4, 0, 0));
			EXPECT_TRUE(GJKRaycast<T>(B, Needle, TRigidTransform<T, 3>(TVector<T, 3>(0), Rotated), TVector<T, 3>(0, -1, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(0, 1, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(0, 1, 0), Eps);
		}
	}


	template <typename T>
	void GJKSphereCapsuleSweep()
	{
		typedef TVector<T, 3> TVector3;
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);
		TCapsule<T> B(TVector<T, 3>(1, 0, 0), TVector<T, 3>(-3, 0, 0), 2);

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		constexpr T Eps = 1e-1;

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			T Time;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//hit offset
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 1, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);
			
			//initial overlap
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(7, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, false, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);

			//MTD
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(7, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -5);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);

			//miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//hit with thickness
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));

			//hit rotated
			const TRotation<T, 3> RotatedDown(TRotation<T, 3>::FromVector(TVector<T, 3>(0, PI * 0.5, 0)));
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//miss rotated
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 8.1), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//hit rotated with inflation
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));

			//near hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 - 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//near miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//degenerate
			TSphere<T, 3> Tiny(TVector<T, 3>(1, 0, 0), 1e-8);
			EXPECT_TRUE(GJKRaycast<T>(A, Tiny, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 8, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 4, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//right at end
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);

			// not far enough
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2 - 1e-2, Time, Position, Normal, 0, InitialDir));
		}
	}


	template <typename T>
	void GJKSphereConvexSweep()
	{
		typedef TVector<T, 3> TVector3;
		//Tetrahedron
		TParticles<T, 3> HullParticles;
		HullParticles.AddParticles(4);
		HullParticles.X(0) = { 3,0,4 };
		HullParticles.X(1) = { 3,1,0 };
		HullParticles.X(2) = { 3,-1,0 };
		HullParticles.X(3) = { 4,0,2 };
		FConvex A(HullParticles);
		TSphere<T, 3> B(TVector<T, 3>(0, 0, 0), 1);

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		constexpr T Eps = 1e-1;

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			T Time;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(3, 0, 0), Eps);

			//hit offset
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1.5, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 0.5, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(3, 0, 0), Eps);

			//initial overlap
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(4, 0, 4), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, false, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);

			//MTD
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2.5, 0, 2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -0.5);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0).GetUnsafeNormal(), Eps);

			//MTD
			T Penetration;
			TVec3<T> ClosestA, ClosestB;
			EXPECT_TRUE(GJKPenetration<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2.5, 0, 2), TRotation<T, 3>::Identity), Penetration, ClosestA, ClosestB, Normal, 0, InitialDir));
			EXPECT_FLOAT_EQ(Penetration, 0.5);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0).GetUnsafeNormal(), Eps);
			EXPECT_NEAR(ClosestA[0], 3, Eps);	//could be any point on face, but should have x == 3
			EXPECT_VECTOR_NEAR(ClosestB, TVector3(3.5, 0, 2), Eps);

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 6), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, -1).GetUnsafeNormal(), 4, Time, Position, Normal, 0, InitialDir));
			const T ExpectedTime = ((TVector<T, 3>(3, 0, 4) - TVector<T, 3>(1, 0, 6)).Size() - 1);
			EXPECT_NEAR(Time, ExpectedTime, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-sqrt(2) / 2, 0, sqrt(2) / 2), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(3, 0, 4), Eps);

			//near miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 5 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));

			//near hit with inflation
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 5 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 2e-2, InitialDir));
			const T DistanceFromCorner = (Position - TVector<T, 3>(3, 0, 4)).Size();
			EXPECT_LT(DistanceFromCorner, 1e-1);

			//rotated box
			const TRotation<T, 3> Rotated(TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI * 0.5)));
			EXPECT_TRUE(GJKRaycast<T>(B, A, TRigidTransform<T, 3>(TVector<T, 3>(0), Rotated), TVector<T, 3>(0, -1, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_NEAR(Normal.X, 0, Eps);
			EXPECT_NEAR(Normal.Y, 1, Eps);
			//EXPECT_NEAR(Normal.Z, 0, Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(0, 1, 0), Eps);

			//degenerate box
			TAABB<T, 3> Needle(TVector<T, 3>(3, 0, 0), TVector<T, 3>(4, 0, 0));
			EXPECT_TRUE(GJKRaycast<T>(B, Needle, TRigidTransform<T, 3>(TVector<T, 3>(0), Rotated), TVector<T, 3>(0, -1, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(0, 1, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(0, 1, 0), Eps);
		}
	}

	template <typename T>
	void GJKSphereScaledSphereSweep()
	{
		typedef TVector<T, 3> TVector3;
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);
		TUniquePtr<TSphere<T, 3>> Sphere = MakeUnique<TSphere<T, 3>>(TVector<T, 3>(0, 0, 0), 2);
		TImplicitObjectScaled<TSphere<T, 3>> Unscaled(MakeSerializable(Sphere), TVector<T, 3>(1));
		TImplicitObjectScaled<TSphere<T, 3>> UniformScaled(MakeSerializable(Sphere), TVector<T, 3>(2));
		TImplicitObjectScaled<TSphere<T, 3>> NonUniformScaled(MakeSerializable(Sphere), TVector<T, 3>(2, 1, 1));

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		constexpr T Eps = 1e-1;

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			T Time;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 3, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			EXPECT_TRUE(GJKRaycast<T>(A, UniformScaled, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 6, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 1, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			EXPECT_TRUE(GJKRaycast<T>(A, NonUniformScaled, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 1, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//hit offset
			EXPECT_TRUE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			EXPECT_TRUE(GJKRaycast<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 0, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			EXPECT_TRUE(GJKRaycast<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 0, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//initial overlap
			EXPECT_TRUE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(8, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);
			EXPECT_TRUE(GJKRaycast<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(6, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);
			EXPECT_TRUE(GJKRaycast<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(6, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);

			//miss
			EXPECT_FALSE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 9.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//hit with thickness
			EXPECT_TRUE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 9.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));

			//hit rotated
			const TRotation<T, 3> RotatedInPlace(TRotation<T, 3>::FromVector(TVector<T, 3>(0, PI * 0.5, 0)));
			EXPECT_TRUE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 0), RotatedInPlace), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 0), RotatedInPlace), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 0), RotatedInPlace), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//miss rotated
			EXPECT_FALSE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), RotatedInPlace), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, UniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 9.1), RotatedInPlace), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, NonUniformScaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 9.1), RotatedInPlace), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//near hit
			EXPECT_TRUE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 - 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//near miss
			EXPECT_FALSE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//degenerate
			TSphere<T, 3> Tiny(TVector<T, 3>(1, 0, 0), 1e-8);
			EXPECT_TRUE(GJKRaycast<T>(A, Tiny, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 8, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 4, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//right at end
			EXPECT_TRUE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 3, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 3, Eps);

			// not far enough
			EXPECT_FALSE(GJKRaycast<T>(A, Unscaled, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 3 - 1e-2, Time, Position, Normal, 0, InitialDir));
		}
	}


	template <typename T>
	void GJKSphereTransformedSphereSweep()
	{
		typedef TVector<T, 3> TVector3;
		TSphere<T, 3> A(TVector<T, 3>(10, 0, 0), 5);

		TSphere<T, 3> Sphere(TVector<T, 3>(0), 2);
		TSphere<T, 3> Translated(Sphere.GetCenter() + TVector<T, 3>(1, 0, 0), Sphere.GetRadius());
		TSphere<T,3> Transformed(TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::FromVector(TVector<T, 3>(0, 0, PI))).TransformPosition(Sphere.GetCenter()), Sphere.GetRadius());

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		constexpr T Eps = 1e-1;

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			T Time;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//hit offset
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 1, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(1, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 1, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVector3(-1, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Position, TVector3(5, 0, 0), Eps);

			//initial overlap
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(7, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(7, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);

			//miss
			EXPECT_FALSE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//hit with thickness
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.1), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));

			//hit rotated
			const TRotation<T, 3> RotatedDown(TRotation<T, 3>::FromVector(TVector<T, 3>(0, PI * 0.5, 0)));
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//miss rotated
			EXPECT_FALSE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 8.1), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 8.1), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//hit rotated with inflation
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7.9), RotatedDown), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0.2, InitialDir));

			//near hit
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 - 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 - 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//near miss
			EXPECT_FALSE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 20, Time, Position, Normal, 0, InitialDir));

			//right at end
			EXPECT_TRUE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);
			EXPECT_TRUE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 2, Eps);

			// not far enough
			EXPECT_FALSE(GJKRaycast<T>(A, Translated, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2 - 1e-2, Time, Position, Normal, 0, InitialDir));
			EXPECT_FALSE(GJKRaycast<T>(A, Transformed, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2 - 1e-2, Time, Position, Normal, 0, InitialDir));
		}
	}


	template <typename T>
	void GJKBoxCapsuleSweep()
	{
		TAABB<T, 3> A(TVector<T, 3>(3, -1, 0), TVector<T, 3>(4, 1, 4));
		TCapsule<T> B(TVector<T, 3>(0, 0, -1), TVector<T, 3>(0, 0, 1), 2);

		TVector<T, 3> InitialDirs[] = { TVector<T,3>(1,0,0), TVector<T,3>(-1,0,0), TVector<T,3>(0,1,0), TVector<T,3>(0,-1,0), TVector<T,3>(0,0,1), TVector<T,3>(0,0,-1) };

		constexpr T Eps = 1e-1;

		for (const TVector<T, 3>& InitialDir : InitialDirs)
		{
			T Time;
			TVector<T, 3> Position;
			TVector<T, 3> Normal;

			//hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 1, Eps);
			EXPECT_NEAR(Normal.X, -1, Eps);
			EXPECT_NEAR(Normal.Y, 0, Eps);
			EXPECT_NEAR(Normal.Z, 0, Eps);
			EXPECT_NEAR(Position.X, 3, Eps);
			//EXPECT_NEAR(Position.Y, 0, Eps);	//todo: look into inaccuracy here (0.015) instead of <1e-2
			EXPECT_LE(Position.Z, 1 + Eps);
			EXPECT_GE(Position.Z, -1 - Eps);

			//hit offset
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0.5, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 0.5, Eps);
			EXPECT_NEAR(Normal.X, -1, Eps);
			EXPECT_NEAR(Normal.Y, 0, Eps);
			EXPECT_NEAR(Normal.Z, 0, Eps);
			EXPECT_NEAR(Position.X, 3, Eps);
			//EXPECT_NEAR(Position.Y, 0, Eps);	//todo: look into inaccuracy here (0.015) instead of <1e-2
			EXPECT_LE(Position.Z, 1 + Eps);
			EXPECT_GE(Position.Z, -1 - Eps);

			//initial overlap
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, false, InitialDir));
			EXPECT_FLOAT_EQ(Time, 0);

			//MTD
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2.5, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -1.5);
			EXPECT_NEAR(Position[0], 3, Eps);	//many possible, but x must be on 3
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(-1, 0, 0), Eps);

			//MTD
			T Penetration;
			TVec3<T> ClosestA, ClosestB;
			EXPECT_TRUE(GJKPenetration<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2.5, 0, 0), TRotation<T, 3>::Identity), Penetration, ClosestA, ClosestB, Normal, 0, InitialDir));
			EXPECT_FLOAT_EQ(Penetration, 1.5);
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(-1, 0, 0), Eps);
			EXPECT_NEAR(ClosestA[0], 3, Eps);	//could be any point on face, but should have x == 3
			EXPECT_NEAR(ClosestB[0], 4.5, Eps);
			EXPECT_NEAR(ClosestB[1], 0, Eps);

			//EPA
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -2);
			EXPECT_NEAR(Position[0], 3, Eps);	//many possible, but x must be on 3
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(-1, 0, 0), Eps);

			//EPA
			EXPECT_TRUE(GJKPenetration<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3, 0, 0), TRotation<T, 3>::Identity), Penetration, ClosestA, ClosestB, Normal, 0, InitialDir));
			EXPECT_NEAR(Penetration, 2, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(-1, 0, 0), Eps);
			EXPECT_NEAR(ClosestA[0], 3, Eps);	//could be any point on face, but should have x == 3
			EXPECT_NEAR(ClosestB[0], 5, Eps);
			EXPECT_NEAR(ClosestB[1], 0, Eps);

			//EPA
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.25, 0, 0), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -2.25);
			EXPECT_NEAR(Position[0], 3, Eps);	//many possible, but x must be on 3
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(-1, 0, 0), Eps);

			//EPA
			EXPECT_TRUE(GJKPenetration<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.25, 0, 0), TRotation<T, 3>::Identity), Penetration, ClosestA, ClosestB, Normal, 0, InitialDir));
			EXPECT_NEAR(Penetration, 2.25, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(-1, 0, 0), Eps);
			EXPECT_NEAR(ClosestA[0], 3, Eps);	//could be any point on face, but should have x == 3
			EXPECT_NEAR(ClosestB[0], 5.25, Eps);
			EXPECT_NEAR(ClosestB[1], 0, Eps);

			//MTD
			EXPECT_TRUE(GJKRaycast2<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.25, 0, -2.875), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 2, Time, Position, Normal, 0, true, InitialDir));
			EXPECT_FLOAT_EQ(Time, -0.125);
			EXPECT_VECTOR_NEAR(Position, TVec3<T>(3.25, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(0, 0, -1), Eps);

			//MTD
			EXPECT_TRUE(GJKPenetration<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(3.25, 0, -2.875), TRotation<T, 3>::Identity), Penetration, ClosestA, ClosestB, Normal, 0, InitialDir));
			EXPECT_NEAR(Penetration, 0.125, Eps);
			EXPECT_VECTOR_NEAR(Normal, TVec3<T>(0, 0, -1), Eps);
			EXPECT_VECTOR_NEAR(ClosestA, TVec3<T>(3.25, 0, 0), Eps);
			EXPECT_VECTOR_NEAR(ClosestB, TVec3<T>(3.25, 0, 0.125), Eps);

			//near miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 + 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));

			//near hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 - 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Position.X, 3, Eps);
			EXPECT_NEAR(Position.Z, 4, 10 * Eps);

			//near hit inflation
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 7 - 1e-2), TRotation<T, 3>::Identity), TVector<T, 3>(1, 0, 0), 4, Time, Position, Normal, 2e-2, InitialDir));
			EXPECT_NEAR(Position.X, 3, Eps);
			EXPECT_NEAR(Position.Z, 4, 10 * Eps);

			//rotation hit
			TRotation<T, 3> Rotated(TRotation<T, 3>::FromVector(TVector<T, 3>(0, -PI * 0.5, 0)));
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(-0.5, 0, 0), Rotated), TVector<T, 3>(1, 0, 0), 1, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 0.5, Eps);
			EXPECT_NEAR(Position.X, 3, Eps);
			EXPECT_NEAR(Normal.X, -1, Eps);
			EXPECT_NEAR(Normal.Y, 0, Eps);
			EXPECT_NEAR(Normal.Z, 0, Eps);

			//rotation near hit
			EXPECT_TRUE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 6 - 1e-2), Rotated), TVector<T, 3>(1, 0, 0), 10, Time, Position, Normal, 0, InitialDir));

			//rotation near miss
			EXPECT_FALSE(GJKRaycast<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(0, 0, 6 + 1e-2), Rotated), TVector<T, 3>(1, 0, 0), 10, Time, Position, Normal, 0, InitialDir));

			//degenerate capsule
			TCapsule<T> Needle(TVector<T, 3>(0, 0, -1), TVector<T, 3>(0, 0, 1), 1e-8);
			EXPECT_TRUE(GJKRaycast<T>(A, Needle, TRigidTransform<T, 3>::Identity, TVector<T, 3>(1, 0, 0), 6, Time, Position, Normal, 0, InitialDir));
			EXPECT_NEAR(Time, 3, Eps);
			EXPECT_NEAR(Normal.X, -1, Eps);
			EXPECT_NEAR(Normal.Y, 0, Eps);
			EXPECT_NEAR(Normal.Z, 0, Eps);
			EXPECT_NEAR(Position.X, 3, Eps);
			//EXPECT_NEAR(Position.Y, 0, Eps);	//todo: look into inaccuracy here (0.015) instead of <1e-2
			EXPECT_LE(Position.Z, 1 + Eps);
			EXPECT_GE(Position.Z, -1 - Eps);
		}
	}

	template <typename T>
	void GJKBoxBoxSweep()
	{
		{
			//based on real sweep from game
			const TAABB<T, 3> A(TVec3<T>(-2560.00000, -268.000031, -768.000122), TVec3<T>(0.000000000, 3.99996948, 0.000000000));
			const TAABB<T, 3> B(TVec3<T>(-248.000000, -248.000000, -9.99999975e-05), TVec3<T>(248.000000, 248.000000, 9.99999975e-05));
			const TRigidTransform<T, 3> BToATM(TVec3<T>(-2559.99780, -511.729492, -8.98901367), TRotation<T, 3>::FromElements(1.51728955e-06, 1.51728318e-06, 0.707108259, 0.707105279));
			const TVec3<T> LocalDir(-4.29153351e-06, 0.000000000, -1.00000000);
			const T Length = 393.000000;
			const TVec3<T> SearchDir(511.718750, -2560.00000, 9.00000000);

			T Time;
			TVec3<T> Pos, Normal;
			GJKRaycast2<T>(A, B, BToATM, LocalDir, Length, Time, Pos, Normal, 0, true, SearchDir, 0);
		}

		{
			//based on real sweep from game
			TParticles<T, 3> ConvexParticles;
			ConvexParticles.AddParticles(10);

			ConvexParticles.X(0) = { 51870.2305, 54369.6719, 19200.0000 };
			ConvexParticles.X(1) = { -91008.5625, -59964.0000, -19199.9629 };
			ConvexParticles.X(2) = { 51870.2305, 54369.6758, -19199.9668 };
			ConvexParticles.X(3) = { 22164.4883, 124647.500, -19199.9961 };
			ConvexParticles.X(4) = { 34478.5000, 123975.492, -19199.9961 };
			ConvexParticles.X(5) = { -91008.5000, -59963.9375, 19200.0000 };
			ConvexParticles.X(6) = { -91008.5000, 33715.5625, 19200.0000 };
			ConvexParticles.X(7) = { 34478.4961, 123975.500, 19200.0000 };
			ConvexParticles.X(8) = { 22164.4922, 124647.500, 19200.0000 };
			ConvexParticles.X(9) = { -91008.5000, 33715.5625, -19199.9961 };


			const FConvex A(ConvexParticles);
			const TAABB<T, 3> B(TVec3<T>{ -6.00000000, -248.000000, -9.99999975e-05 }, TVec3<T>{ 6.00000000, 248.000000, 9.99999975e-05 });
			const TRigidTransform<T, 3> BToATM(TVec3<T>{33470.5000, 41570.5000, -1161.00000}, TRotation<T, 3>::FromIdentity());
			const TVec3<T> LocalDir(0, 0, -1);
			const T Length = 393.000000;
			const TVec3<T> SearchDir{ -33470.5000, -41570.5000, 1161.00000 };

			T Time;
			TVec3<T> Pos, Normal;
			GJKRaycast2<T>(A, B, BToATM, LocalDir, Length, Time, Pos, Normal, 0, true, SearchDir, 0);
		}
	}

	template <typename T>
	void GJKCapsuleConvexInitialOverlapSweep()
	{
		TParticles<T, 3> ConvexParticles;
		ConvexParticles.AddParticles(8);

		ConvexParticles.X(0) = {-256.000031, 12.0000601, 384.000061};
		ConvexParticles.X(1) = {256.000031, 12.0000601, 384.000061};
		ConvexParticles.X(2) = {256.000031, 12.0000601, 6.10351563e-05};
		ConvexParticles.X(3) = {-256.000031, -11.9999399, 6.10351563e-05};
		ConvexParticles.X(4) = {-256.000031, 12.0000601, 6.10351563e-05};
		ConvexParticles.X(5) = {-256.000031, -11.9999399, 384.000061};
		ConvexParticles.X(6) = {256.000031, -11.9999399, 6.10351563e-05};
		ConvexParticles.X(7) = {256.000031, -11.9999399, 384.000061};

		TUniquePtr<FConvex> UniqueConvex = MakeUnique<FConvex>(ConvexParticles);
		TSerializablePtr<FConvex> AConv(UniqueConvex);
		const TImplicitObjectScaled<FConvex> A(AConv, TVec3<T>(1.0, 1.0, 1.0));

		const TVec3<T> Pt0(0.0, 0.0, -33.0);
		TVec3<T> Pt1 = Pt0;
		Pt1 += (TVec3<T>(0.0, 0.0, 1.0) * 66.0);

		const TCapsule<T> B(Pt0, Pt1, 42.0);

		const TRigidTransform<T, 3> BToATM(TVec3<T>(157.314758, -54.0000839, 76.1436157), TRotation<T, 3>::FromElements(0.0, 0.0, 0.704960823, 0.709246278));
		const TVec3<T> LocalDir(-0.00641351938, -0.999979556, 0.0);
		const T Length = 0.0886496082;
		const TVec3<T> SearchDir(-3.06152344, 166.296631, -76.1436157);

		T Time;
		TVec3<T> Position, Normal;
		EXPECT_TRUE(GJKRaycast2<T>(A, B, BToATM, LocalDir, Length, Time, Position, Normal, 0, true, SearchDir, 0));
		EXPECT_FLOAT_EQ(Time, 0.0);
	}

	template void SimplexLine<float>();
	template void SimplexTriangle<float>();
	template void SimplexTetrahedron<float>();
	template void GJKSphereSphereTest<float>();
	template void GJKSphereBoxTest<float>();
	template void GJKSphereCapsuleTest<float>();
	template void GJKSphereConvexTest<float>();
	template void GJKSphereScaledSphereTest<float>();
	
	template void GJKSphereSphereSweep<float>();
	template void GJKSphereBoxSweep<float>();
	template void GJKSphereCapsuleSweep<float>();
	template void GJKSphereConvexSweep<float>();
	template void GJKSphereScaledSphereSweep<float>();
	template void GJKSphereTransformedSphereSweep<float>();
	template void GJKBoxCapsuleSweep<float>();
	template void GJKBoxBoxSweep<float>();
	template void GJKCapsuleConvexInitialOverlapSweep<float>();
}