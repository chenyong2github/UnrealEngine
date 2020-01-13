// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestGJK.h"
#include "HeadlessChaos.h"

#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/GJK.h"
#include "Chaos/ImplicitObjectScaled.h"

namespace ChaosTest
{
	using namespace Chaos;



	template <typename T>
	void GJKSphereSphereDistanceTest()
	{
		const T Tolerance = (T)1e-3;

		TVector<T, 3> NearestA = { 0,0,0 };
		TVector<T, 3> NearestB = { 0,0,0 };
		T Distance = 0;
		// Fail - overlapping
		{
			TSphere<T, 3> A(TVector<T, 3>(12, 0, 0), 5);
			TSphere<T, 3> B(TVector<T, 3>(4, 0, 0), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2, 0, 0), TRotation<T, 3>::FromIdentity()), Distance, NearestA, NearestB);
			EXPECT_FALSE(bSuccess);
		}

		// Success - not overlapping
		{
			TSphere<T, 3> A(TVector<T, 3>(12, 0, 0), 5);
			TSphere<T, 3> B(TVector<T, 3>(4, 0, 0), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB);
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (T)1, Tolerance);
			EXPECT_NEAR(NearestA.X, (T)7, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestA.Z, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.X, (T)6, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.Z, (T)0, Tolerance);
		}

		// Success - not overlapping
		{
			TSphere<T, 3> A(TVector<T, 3>(0, 0, 0), 2);
			TSphere<T, 3> B(TVector<T, 3>(0, 0, 0), 2);
			TVector<T, 3> BPos = TVector<T, 3>(3, 3, 0);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>(BPos, TRotation<T, 3>::FromIdentity()), Distance, NearestA, NearestB);
			EXPECT_TRUE(bSuccess);
			TVector<T, 3> CenterDelta = (B.GetCenter() + BPos) - A.GetCenter();
			TVector<T, 3> CenterDir = CenterDelta.GetSafeNormal();
			EXPECT_NEAR(Distance, CenterDelta.Size() - (A.GetRadius() + B.GetRadius()), Tolerance);
			EXPECT_NEAR(NearestA.X, A.GetCenter().X + A.GetRadius() * CenterDir.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, A.GetCenter().Y + A.GetRadius() * CenterDir.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, A.GetCenter().Z + A.GetRadius() * CenterDir.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, B.GetCenter().X - B.GetRadius() * CenterDir.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, B.GetCenter().Y - B.GetRadius() * CenterDir.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, B.GetCenter().Z - B.GetRadius() * CenterDir.Z, Tolerance);
		}

		// Success - very close not overlapping
		{
			TSphere<T, 3> A(TVector<T, 3>(12, 0, 0), 5);
			TSphere<T, 3> B(TVector<T, 3>(4, 0, 0), 2);
			TVector<T, 3> BPos = TVector<T, 3>(0.99, 0, 0);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>(BPos, TRotation<T, 3>::FromIdentity()), Distance, NearestA, NearestB);
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (T)1 - BPos.X, Tolerance);
			EXPECT_NEAR(NearestA.X, (T)7, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestA.Z, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.X, (T)6, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.Z, (T)0, Tolerance);
		}
	}

	TEST(TestGJKDistance, GJKSphereSphereDistanceTest)
	{
		GJKSphereSphereDistanceTest<float>();
	}

	template <typename T>
	void GJKBoxSphereDistanceTest()
	{
		const T Tolerance = (T)2e-3;

		TVector<T, 3> NearestA = { 0,0,0 };
		TVector<T, 3> NearestB = { 0,0,0 };
		T Distance = 0;

		// Fail - overlapping
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, -2), TVector<T, 3>(8, 2, 2));
			TSphere<T, 3> B(TVector<T, 3>(2, 0, 0), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2, 0, 0), TRotation<T, 3>::FromIdentity()), Distance, NearestA, NearestB);
			EXPECT_FALSE(bSuccess);
		}

		// Success - not overlapping - mid-face near point
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, -2), TVector<T, 3>(8, 2, 2));
			TSphere<T, 3> B(TVector<T, 3>(2, 0, 0), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB);
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (T)1, Tolerance);
			EXPECT_NEAR(NearestA.X, (T)5, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestA.Z, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.X, (T)4, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.Z, (T)0, Tolerance);
		}
		// Other way round
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, -2), TVector<T, 3>(8, 2, 2));
			TSphere<T, 3> B(TVector<T, 3>(2, 0, 0), 2);
			bool bSuccess = GJKDistance<T>(B, A, TRigidTransform<T, 3>::Identity, Distance, NearestB, NearestA);
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (T)1, Tolerance);
			EXPECT_NEAR(NearestA.X, (T)5, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestA.Z, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.X, (T)4, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)0, Tolerance);
			EXPECT_NEAR(NearestB.Z, (T)0, Tolerance);
		}

		// Success - not overlapping - vertex near point
		{
			TAABB<T, 3> A(TVector<T, 3>(5, 2, 2), TVector<T, 3>(8, 4, 4));
			TSphere<T, 3> B(TVector<T, 3>(2, 0, 0), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB);
			TVector<T, 3> NearPointOnA = A.Min();
			TVector<T, 3> SphereNearPointDir = (NearPointOnA - B.GetCenter()).GetSafeNormal();
			TVector<T, 3> NearPointOnB = B.GetCenter() + SphereNearPointDir * B.GetRadius();
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (NearPointOnA - NearPointOnB).Size(), Tolerance);
			EXPECT_NEAR(NearestA.X, NearPointOnA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, NearPointOnA.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, NearPointOnA.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, NearPointOnB.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, NearPointOnB.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, NearPointOnB.Z, Tolerance);
		}
		// Other way round
		{
			TAABB<T, 3> A(TVector<T, 3>(5, 2, 2), TVector<T, 3>(8, 4, 4));
			TSphere<T, 3> B(TVector<T, 3>(2, 0, 0), 2);
			bool bSuccess = GJKDistance<T>(B, A, TRigidTransform<T, 3>::Identity, Distance, NearestB, NearestA);
			TVector<T, 3> NearPointOnA = A.Min();
			TVector<T, 3> SphereNearPointDir = (NearPointOnA - B.GetCenter()).GetSafeNormal();
			TVector<T, 3> NearPointOnB = B.GetCenter() + SphereNearPointDir * B.GetRadius();
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (NearPointOnA - NearPointOnB).Size(), Tolerance);
			EXPECT_NEAR(NearestA.X, NearPointOnA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, NearPointOnA.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, NearPointOnA.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, NearPointOnB.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, NearPointOnB.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, NearPointOnB.Z, Tolerance);
		}

		// Rotated
		{
			TAABB<T, 3> A(TVector<T, 3>(-2, -2, -2), TVector<T, 3>(4, 4, 4));
			TSphere<T, 3> B(TVector<T, 3>(0, 0, 0), 2);
			TRigidTransform<T, 3> BToATm = TRigidTransform<T, 3>(TVector<T, 3>(8, 0, 0), TRotation<T, 3>::FromAxisAngle(TVector<T, 3>(0, 1, 0), FMath::DegreesToRadians(45)));	// Rotation won't affect sphere
			bool bSuccess = GJKDistance<T>(A, B, BToATm, Distance, NearestA, NearestB);
			TVector<T, 3> NearPointOnA = TVector<T, 3>(4, 0, 0);
			TVector<T, 3> BPos = BToATm.TransformPositionNoScale(B.GetCenter());
			TVector<T, 3> NearPointDir = (NearPointOnA - BPos).GetSafeNormal();
			TVector<T, 3> NearPointOnB = BPos + NearPointDir * B.GetRadius();
			TVector<T, 3> NearPointOnBLocal = BToATm.InverseTransformPositionNoScale(NearPointOnB);
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (NearPointOnA - NearPointOnB).Size(), Tolerance);
			EXPECT_NEAR(NearestA.X, NearPointOnA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, NearPointOnA.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, NearPointOnA.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, NearPointOnBLocal.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, NearPointOnBLocal.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, NearPointOnBLocal.Z, Tolerance);
		}
		// Other way round
		{
			TAABB<T, 3> A(TVector<T, 3>(-2, -2, -2), TVector<T, 3>(4, 4, 4));
			TSphere<T, 3> B(TVector<T, 3>(0, 0, 0), 2);
			TRigidTransform<T, 3> BToATm = TRigidTransform<T, 3>(TVector<T, 3>(-8, 0, 0), TRotation<T, 3>::FromAxisAngle(TVector<T, 3>(0, 1, 0), FMath::DegreesToRadians(45)));	// Rotation will affect box
			bool bSuccess = GJKDistance<T>(B, A, BToATm, Distance, NearestB, NearestA);
			TVector<T, 3> NearPointOnA = TVector<T, 3>(4, 0, 4);
			TVector<T, 3> BPos = BToATm.InverseTransformPositionNoScale(B.GetCenter());
			TVector<T, 3> NearPointDir = (NearPointOnA - BPos).GetSafeNormal();
			TVector<T, 3> NearPointOnB = BPos + NearPointDir * B.GetRadius();
			TVector<T, 3> NearPointOnBLocal = BToATm.TransformPositionNoScale(NearPointOnB);
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (NearPointOnA - NearPointOnB).Size(), Tolerance);
			EXPECT_NEAR(NearestA.X, NearPointOnA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, NearPointOnA.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, NearPointOnA.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, NearPointOnBLocal.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, NearPointOnBLocal.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, NearPointOnBLocal.Z, Tolerance);
		}
	
		// Success - specific test case that initially failed (using incorrect initialization of V which works for Overlap but not Distance)
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, 2), TVector<T, 3>(8, 2, 4));
			TSphere<T, 3> B(TVector<T, 3>(2, 0, 0), 2);

			bool bOverlap = GJKIntersection<T>(A, B, TRigidTransform<T, 3>::Identity);
			EXPECT_FALSE(bOverlap);

			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB);
			TVector<T, 3> NearPointOnA = TVector<T, 3>(5, 0, 2);
			TVector<T, 3> NearPointDir = (NearPointOnA - B.GetCenter()).GetSafeNormal();
			TVector<T, 3> NearPointOnB = B.GetCenter() + NearPointDir * B.GetRadius();
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (NearPointOnA - NearPointOnB).Size(), Tolerance);
			EXPECT_NEAR(NearestA.X, NearPointOnA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, NearPointOnA.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, NearPointOnA.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, NearPointOnB.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, NearPointOnB.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, NearPointOnB.Z, Tolerance);
		}
	}

	TEST(TestGJKDistance, GJKBoxSphereDistanceTest)
	{
		GJKBoxSphereDistanceTest<float>();
	}

	template <typename T>
	void GJKBoxCapsuleDistanceTest()
	{
		TVector<T, 3> NearestA = { 0,0,0 };
		TVector<T, 3> NearestB = { 0,0,0 };
		T Distance = 0;

		// Fail - overlapping
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, -2), TVector<T, 3>(8, 2, 2));
			TCapsule<T> B(TVector<T, 3>(2, -2, 0), TVector<T, 3>(2, 2, 0), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>(TVector<T, 3>(2, 0, 0), TRotation<T, 3>::FromIdentity()), Distance, NearestA, NearestB);
			EXPECT_FALSE(bSuccess);
		}

		// Success - not overlapping, capsule axis parallel to nearest face (near points on cylinder and box face)
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, -2), TVector<T, 3>(8, 2, 2));
			TCapsule<T> B(TVector<T, 3>(2, 0, -1), TVector<T, 3>(2, 0, 2), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB);

			const T Tolerance = (T)2e-3;
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (T)1, Tolerance);
			EXPECT_NEAR(NearestA.X, (T)5, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)0, Tolerance);
			EXPECT_GT(NearestA.Z, (T)-2-Tolerance);
			EXPECT_LT(NearestA.Z, (T)2+Tolerance);
			EXPECT_NEAR(NearestB.X, (T)4, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)0, Tolerance);
			EXPECT_GT(NearestB.Z, (T)-1-Tolerance);
			EXPECT_LT(NearestB.Z, (T)2+Tolerance);
		}

		// Success - not overlapping, capsule axis at angle to nearest face (near points on end-cap and box edge)
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, -2), TVector<T, 3>(8, 2, 2));
			TCapsule<T> B(TVector<T, 3>(-2, 0, 3), TVector<T, 3>(2, 0, -3), 2);
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB);
			TVector<T, 3> ExpectedNearestA = TVector<T, 3>(5, 0, -2);
			TVector<T, 3> ExpectedDir = (ExpectedNearestA - B.GetX2()).GetSafeNormal();
			TVector<T, 3> ExpectedNearestB = B.GetX2() + ExpectedDir * B.GetRadius();

			const T Tolerance = (T)2e-3;
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (ExpectedNearestB - ExpectedNearestA).Size(), Tolerance);
			EXPECT_NEAR(NearestA.X, (T)ExpectedNearestA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)ExpectedNearestA.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, (T)ExpectedNearestA.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, (T)ExpectedNearestB.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)ExpectedNearestB.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, (T)ExpectedNearestB.Z, Tolerance);
		}

		// Success - not overlapping, near point partway down wall of capsule
		{
			TCapsule<T> A(TVector<T, 3>(4, 0, -1), TVector<T, 3>(4, 0, -7), 1);
			TAABB<T, 3> B(TVector<T, 3>(-2, -2, -2), TVector<T, 3>(2, 2, 2));
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB);
			TVector<T, 3> ExpectedNearestA = TVector<T, 3>(3, 0, (T)-1.5);
			TVector<T, 3> ExpectedNearestB = TVector<T, 3>(2, 0, (T)-1.5);

			const T Tolerance = (T)2e-3;
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (T)1, Tolerance);
			EXPECT_NEAR(NearestA.X, (T)ExpectedNearestA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)ExpectedNearestA.Y, Tolerance);
			EXPECT_LT(NearestA.Z, (T)ExpectedNearestA.Z + (T)0.5 + Tolerance);
			EXPECT_GT(NearestA.Z, (T)ExpectedNearestA.Z - (T)0.5 - Tolerance);
			EXPECT_NEAR(NearestB.X, (T)ExpectedNearestB.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)ExpectedNearestB.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, (T)NearestA.Z, Tolerance);
		}

		// Success - not overlapping, near point partway down wall of capsule.
		// Same result as above, but using transform rather than the shape's built-in offsets.
		{
			TCapsule<T> A(TVector<T, 3>(0, 0, -3), TVector<T, 3>(0, 0, 3), 1);
			TAABB<T, 3> B(TVector<T, 3>(-2, -2, -2), TVector<T, 3>(2, 2, 2));
			TRigidTransform<T, 3> BToA = TRigidTransform<T, 3>(TVector<T, 3>(-4, 0, 4), TRotation<T, 3>::FromIdentity());
			bool bSuccess = GJKDistance<T>(A, B, BToA, Distance, NearestA, NearestB);
			TVector<T, 3> ExpectedNearestA = TVector<T, 3>(-1, 0, (T)2);
			TVector<T, 3> ExpectedNearestB = TVector<T, 3>(2, 0, (T)-2);

			const T Tolerance = (T)2e-3;
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (T)1, Tolerance);
			EXPECT_NEAR(NearestA.X, (T)ExpectedNearestA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)ExpectedNearestA.Y, Tolerance);
			EXPECT_LT(NearestA.Z, (T)ExpectedNearestA.Z + (T)0.5 + Tolerance);
			EXPECT_GT(NearestA.Z, (T)ExpectedNearestA.Z - (T)0.5 - Tolerance);
			EXPECT_NEAR(NearestB.X, (T)ExpectedNearestB.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)ExpectedNearestB.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z + BToA.GetTranslation().Z, (T)NearestA.Z, Tolerance);
		}
	}


	TEST(TestGJKDistance, GJKBoxCapsuleDistanceTest)
	{
		GJKBoxCapsuleDistanceTest<float>();
	}


	template <typename T>
	void GJKBoxCapsuleDistanceIterationCountTest()
	{
		TVector<T, 3> NearestA = { 0,0,0 };
		TVector<T, 3> NearestB = { 0,0,0 };
		T Distance = 0;

		// Capsule-box takes number of iterations at the moment (we can improve that with a better the choice of Initial V)
		// so test that we still get an approximate answer with less iterations
		{
			TAABB<T, 3> A(TVector<T, 3>(5, -2, -2), TVector<T, 3>(8, 2, 2));
			TCapsule<T> B(TVector<T, 3>(-2, 0, 3), TVector<T, 3>(2, 0, -3), 2);
			T Epsilon = (T)1e-6;
			int32 MaxIts = 5;
			bool bSuccess = GJKDistance<T>(A, B, TRigidTransform<T, 3>::Identity, Distance, NearestA, NearestB, Epsilon, MaxIts);
			TVector<T, 3> ExpectedNearestA = TVector<T, 3>(5, 0, -2);
			TVector<T, 3> ExpectedDir = (ExpectedNearestA - B.GetX2()).GetSafeNormal();
			TVector<T, 3> ExpectedNearestB = B.GetX2() + ExpectedDir * B.GetRadius();

			const T Tolerance = (T)0.3;
			EXPECT_TRUE(bSuccess);
			EXPECT_NEAR(Distance, (ExpectedNearestB - ExpectedNearestA).Size(), Tolerance);
			EXPECT_NEAR(NearestA.X, (T)ExpectedNearestA.X, Tolerance);
			EXPECT_NEAR(NearestA.Y, (T)ExpectedNearestA.Y, Tolerance);
			EXPECT_NEAR(NearestA.Z, (T)ExpectedNearestA.Z, Tolerance);
			EXPECT_NEAR(NearestB.X, (T)ExpectedNearestB.X, Tolerance);
			EXPECT_NEAR(NearestB.Y, (T)ExpectedNearestB.Y, Tolerance);
			EXPECT_NEAR(NearestB.Z, (T)ExpectedNearestB.Z, Tolerance);
		}
	}


	TEST(TestGJKDistance, GJKBoxCapsuleDistanceIterationCountTest)
	{
		GJKBoxCapsuleDistanceIterationCountTest<float>();
	}

}