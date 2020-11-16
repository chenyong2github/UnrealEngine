// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestCollisions.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosCollisionConstraints.h"
#include "Chaos/Box.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Convex.h"
#include "Chaos/Sphere.h"
#include "Chaos/GJK.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/Utilities.h"
#include "Modules/ModuleManager.h"


namespace ChaosTest {

	using namespace Chaos;

	// Two boxes that use a margin around a core AABB.
	// Test that collision detection treats the margin as part of the shape.
	void TestBoxBoxCollisionMargin(
		const FReal Margin0,
		const FReal Margin1,
		const FVec3& Size,
		const FVec3& Delta,
		const FReal ExpectedPhi,
		const FVec3& ExpectedNormal)
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = 0;
		PhysicsMaterial->Restitution = 0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;

		TPBDRigidsSOAs<FReal, 3> Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Box0 = AppendDynamicParticleBoxMargin<FReal>(Particles, Size, Margin0);
		Box0->X() = FVec3(0, 0, 0);
		Box0->R() = FRotation3(FQuat::Identity);
		Box0->V() = FVec3(0);
		Box0->PreV() = Box0->V();
		Box0->P() = Box0->X();
		Box0->Q() = Box0->R();
		Box0->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		auto Box1 = AppendDynamicParticleBoxMargin<FReal>(Particles, Size, Margin1);
		Box1->X() = Delta;
		Box1->R() = FRotation3(FQuat::Identity);
		Box1->V() = FVec3(0);
		Box1->PreV() = Box1->V();
		Box1->P() = Box1->X();
		Box1->Q() = Box1->R();
		Box1->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FImplicitBox3* BoxImplicit0 = Box0->Geometry()->template GetObject<FImplicitBox3>();
		const FImplicitBox3* BoxImplicit1 = Box1->Geometry()->template GetObject<FImplicitBox3>();

		const FReal Tolerance = 2.0f * KINDA_SMALL_NUMBER;

		// Boxes should have a margin
		EXPECT_NEAR(BoxImplicit0->GetMargin(), Margin0, Tolerance);
		EXPECT_NEAR(BoxImplicit1->GetMargin(), Margin1, Tolerance);

		// Core shape should not include margin unless margin is larger than the size
		EXPECT_NEAR(BoxImplicit0->GetCore().Extents().X, FMath::Max(Size.X - 2.0f * Margin0, 0.0f), Tolerance);
		EXPECT_NEAR(BoxImplicit0->GetCore().Extents().Y, FMath::Max(Size.Y - 2.0f * Margin0, 0.0f), Tolerance);
		EXPECT_NEAR(BoxImplicit0->GetCore().Extents().Z, FMath::Max(Size.Z - 2.0f * Margin0, 0.0f), Tolerance);
		EXPECT_NEAR(BoxImplicit1->GetCore().Extents().X, FMath::Max(Size.X - 2.0f * Margin1, 0.0f), Tolerance);
		EXPECT_NEAR(BoxImplicit1->GetCore().Extents().Y, FMath::Max(Size.Y - 2.0f * Margin1, 0.0f), Tolerance);
		EXPECT_NEAR(BoxImplicit1->GetCore().Extents().Z, FMath::Max(Size.Z - 2.0f * Margin1, 0.0f), Tolerance);

		// Box Bounds should include margin, but may be expanded if margin was larger than size
		const FAABB3 BoxBounds0 = BoxImplicit0->BoundingBox();
		const FAABB3 BoxBounds1 = BoxImplicit1->BoundingBox();
		EXPECT_NEAR(BoxBounds0.Extents().X, FMath::Max(2.0f * Margin0, Size.X), Tolerance);
		EXPECT_NEAR(BoxBounds0.Extents().Y, FMath::Max(2.0f * Margin0, Size.Y), Tolerance);
		EXPECT_NEAR(BoxBounds0.Extents().Z, FMath::Max(2.0f * Margin0, Size.Z), Tolerance);
		EXPECT_NEAR(BoxBounds1.Extents().X, FMath::Max(2.0f * Margin1, Size.X), Tolerance);
		EXPECT_NEAR(BoxBounds1.Extents().Y, FMath::Max(2.0f * Margin1, Size.Y), Tolerance);
		EXPECT_NEAR(BoxBounds1.Extents().Z, FMath::Max(2.0f * Margin1, Size.Z), Tolerance);

		FRigidBodyPointContactConstraint Constraint(
			Box0,
			Box0->Geometry().Get(),
			nullptr,
			FRigidTransform3(),
			Box1,
			Box1->Geometry().Get(),
			nullptr,
			FRigidTransform3(),
			EContactShapesType::BoxBox, true, false);

		// Detect collisions
		Collisions::Update(Constraint, Delta.Size(), 1/30.0f);

		EXPECT_NEAR(Constraint.Manifold.Phi, ExpectedPhi, Tolerance);
		EXPECT_NEAR(Constraint.Manifold.Normal.X, ExpectedNormal.X, Tolerance);
		EXPECT_NEAR(Constraint.Manifold.Normal.Y, ExpectedNormal.Y, Tolerance);
		EXPECT_NEAR(Constraint.Manifold.Normal.Z, ExpectedNormal.Z, Tolerance);
	}

	TEST(CollisionTests, TestBoxBoxCollisionMargin)
	{
		// Zero-phi tests
		TestBoxBoxCollisionMargin(0, 0, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(1, 1, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(5, 10, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(10, 5, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));

		// Positive-phi test
		TestBoxBoxCollisionMargin(0, 0, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(1, 1, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(5, 10, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(10, 5, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));

		// Negative-phi test
		TestBoxBoxCollisionMargin(0, 0, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(1, 1, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(5, 10, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));
		TestBoxBoxCollisionMargin(10, 5, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));

		// Rounded Corner test
		TestBoxBoxCollisionMargin(5, 5, FVec3(100, 100, 100), FVec3(-110, -110, -110), FVec3(10).Size() + 2.0f * (FVec3(5).Size() - 5), FVec3(1).GetSafeNormal());

		// If the margin is too large, the box will effectively be larger than specified in some directions
		TestBoxBoxCollisionMargin(15, 15, FVec3(20, 100, 100), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));	// OK - Y Size is larger than margin
		TestBoxBoxCollisionMargin(15, 15, FVec3(20, 100, 100), FVec3(20, 0, 0), -10.0f, FVec3(-1, 0, 0));	// Body X size was expanded to account for margin - they overlap on X
	}

	// Two boxes that use a margin around a core AABB.
	// Test that collision detection treats the margin as part of the shape.
	void TestConvexConvexCollisionMargin(
		const FReal Margin0,
		const FReal Margin1,
		const FVec3& Size,
		const FVec3& Delta,
		const FReal ExpectedPhi,
		const FVec3& ExpectedNormal)
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = 0;
		PhysicsMaterial->Restitution = 0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;

		TPBDRigidsSOAs<FReal, 3> Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Box0 = AppendDynamicParticleConvexBoxMargin<FReal>(Particles, 0.5f * Size, Margin0);
		Box0->X() = FVec3(0, 0, 0);
		Box0->R() = FRotation3(FQuat::Identity);
		Box0->V() = FVec3(0);
		Box0->PreV() = Box0->V();
		Box0->P() = Box0->X();
		Box0->Q() = Box0->R();
		Box0->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		auto Box1 = AppendDynamicParticleConvexBoxMargin<FReal>(Particles, 0.5f * Size, Margin1);
		Box1->X() = Delta;
		Box1->R() = FRotation3(FQuat::Identity);
		Box1->V() = FVec3(0);
		Box1->PreV() = Box1->V();
		Box1->P() = Box1->X();
		Box1->Q() = Box1->R();
		Box1->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FImplicitConvex3* ConvexImplicit0 = Box0->Geometry()->template GetObject<FImplicitConvex3>();
		const FImplicitConvex3* ConvexImplicit1 = Box1->Geometry()->template GetObject<FImplicitConvex3>();

		const FReal Tolerance = 2.0f * KINDA_SMALL_NUMBER;

		// Boxes should have a margin
		EXPECT_NEAR(ConvexImplicit0->GetMargin(), Margin0, Tolerance);
		EXPECT_NEAR(ConvexImplicit1->GetMargin(), Margin1, Tolerance);

		// Box Bounds should include margin, but may be expanded if margin was larger than size
		const FAABB3 BoxBounds0 = ConvexImplicit0->BoundingBox();
		const FAABB3 BoxBounds1 = ConvexImplicit1->BoundingBox();
		EXPECT_NEAR(BoxBounds0.Extents().X, FMath::Max(2.0f * Margin0, Size.X), Tolerance);
		EXPECT_NEAR(BoxBounds0.Extents().Y, FMath::Max(2.0f * Margin0, Size.Y), Tolerance);
		EXPECT_NEAR(BoxBounds0.Extents().Z, FMath::Max(2.0f * Margin0, Size.Z), Tolerance);
		EXPECT_NEAR(BoxBounds1.Extents().X, FMath::Max(2.0f * Margin1, Size.X), Tolerance);
		EXPECT_NEAR(BoxBounds1.Extents().Y, FMath::Max(2.0f * Margin1, Size.Y), Tolerance);
		EXPECT_NEAR(BoxBounds1.Extents().Z, FMath::Max(2.0f * Margin1, Size.Z), Tolerance);

		FRigidBodyPointContactConstraint Constraint(
			Box0,
			Box0->Geometry().Get(),
			nullptr,
			FRigidTransform3(),
			Box1,
			Box1->Geometry().Get(),
			nullptr,
			FRigidTransform3(),
			EContactShapesType::ConvexConvex, true, false);

		// Detect collisions
		Collisions::Update(Constraint, Delta.Size(), 1 / 30.0f);

		EXPECT_NEAR(Constraint.Manifold.Phi, ExpectedPhi, Tolerance);
		EXPECT_NEAR(Constraint.Manifold.Normal.X, ExpectedNormal.X, Tolerance);
		EXPECT_NEAR(Constraint.Manifold.Normal.Y, ExpectedNormal.Y, Tolerance);
		EXPECT_NEAR(Constraint.Manifold.Normal.Z, ExpectedNormal.Z, Tolerance);
	}


	TEST(CollisionTests, DISABLED_TestConvexConvexCollisionMargin)
	{
		// Zero-phi tests
		TestConvexConvexCollisionMargin(0, 0, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(1, 1, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(5, 10, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(10, 5, FVec3(20, 100, 50), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));

		// Positive-phi test
		TestConvexConvexCollisionMargin(0, 0, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(1, 1, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(5, 10, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(10, 5, FVec3(20, 100, 50), FVec3(0, -110, 0), 10.0f, FVec3(0, 1, 0));

		// Negative-phi test
		TestConvexConvexCollisionMargin(0, 0, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(1, 1, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(5, 10, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));
		TestConvexConvexCollisionMargin(10, 5, FVec3(20, 100, 50), FVec3(0, -90, 0), -10.0f, FVec3(0, 1, 0));

		// Rounded Corner test
		TestConvexConvexCollisionMargin(5, 5, FVec3(100, 100, 100), FVec3(-110, -110, -110), FVec3(10).Size() + 2.0f * (FVec3(5).Size() - 5), FVec3(1).GetSafeNormal());
	}

	TEST(CollisionTests, DISABLED_TestConvexConvexCollisionMargin2)
	{
		// @todo(chaos): fix this for convex
		// If the margin is too large, the box will effectively be larger than specified in some directions
		TestConvexConvexCollisionMargin(15, 15, FVec3(20, 100, 100), FVec3(0, -100, 0), 0.0f, FVec3(0, 1, 0));	// OK - Y Size is larger than margin
		TestConvexConvexCollisionMargin(15, 15, FVec3(20, 100, 100), FVec3(20, 0, 0), -10.0f, FVec3(-1, 0, 0));	// Body X size was expanded to account for margin - they overlap on X
	}


	// Check that the margin does not impact the box raycast functions
	void TestBoxRayCastsMargin(
		const FReal Margin0,
		const FVec3& Size,
		const FVec3& StartPos,
		const FVec3& Dir,
		const FReal Length,
		const bool bExpectedHit,
		const FReal ExpectedTime,
		const FVec3& ExpectedPosition,
		const FVec3& ExpectedNormal)
	{
		TArrayCollectionArray<bool> Collided;
		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->Friction = 0;
		PhysicsMaterial->Restitution = 0;
		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
		TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;

		TPBDRigidsSOAs<FReal, 3> Particles;
		Particles.GetParticleHandles().AddArray(&Collided);
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);

		auto Box0 = AppendDynamicParticleBoxMargin<FReal>(Particles, Size, Margin0);
		Box0->X() = FVec3(0, 0, 0);
		Box0->R() = FRotation3(FQuat::Identity);
		Box0->V() = FVec3(0);
		Box0->PreV() = Box0->V();
		Box0->P() = Box0->X();
		Box0->Q() = Box0->R();
		Box0->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		const FImplicitBox3* BoxImplicit0 = Box0->Geometry()->template GetObject<FImplicitBox3>();

		const FReal Tolerance = KINDA_SMALL_NUMBER;

		{
			FReal Time;
			FVec3 Position, Normal;
			int32 FaceIndex;
			bool bHit = BoxImplicit0->Raycast(StartPos, Dir, Length, 0.0f, Time, Position, Normal, FaceIndex);

			EXPECT_EQ(bHit, bExpectedHit);
			if (bHit)
			{
				EXPECT_NEAR(Time, ExpectedTime, Tolerance);
				EXPECT_NEAR(Position.X, ExpectedPosition.X, Tolerance);
				EXPECT_NEAR(Position.Y, ExpectedPosition.Y, Tolerance);
				EXPECT_NEAR(Position.Z, ExpectedPosition.Z, Tolerance);
				EXPECT_NEAR(Normal.X, ExpectedNormal.X, Tolerance);
				EXPECT_NEAR(Normal.Y, ExpectedNormal.Y, Tolerance);
				EXPECT_NEAR(Normal.Z, ExpectedNormal.Z, Tolerance);
			}
		}

		{
			FReal Time;
			FVec3 Position;

			bool bParallel[3];
			FVec3 InvDir;
			for (int Axis = 0; Axis < 3; ++Axis)
			{
				bParallel[Axis] = FMath::IsNearlyZero(Dir[Axis], 1.e-8f);
				InvDir[Axis] = bParallel[Axis] ? 0 : 1 / Dir[Axis];
			}

			bool bHit = BoxImplicit0->RaycastFast(BoxImplicit0->Min(), BoxImplicit0->Max(), StartPos, Dir, InvDir, bParallel, Length, 1.0f / Length, Time, Position);

			EXPECT_EQ(bHit, bExpectedHit);
			if (bHit)
			{
				EXPECT_NEAR(Time, ExpectedTime, Tolerance);
				EXPECT_NEAR(Position.X, ExpectedPosition.X, Tolerance);
				EXPECT_NEAR(Position.Y, ExpectedPosition.Y, Tolerance);
				EXPECT_NEAR(Position.Z, ExpectedPosition.Z, Tolerance);
			}
		}
	}

	TEST(CollisionTests, TestBoxRayCastsMargin)
	{
		TestBoxRayCastsMargin(0, FVec3(100, 100, 100), FVec3(-200, 0, 0), FVec3(1, 0, 0), 500.0f, true, 150.0f, FVec3(-50, 0, 0), FVec3(-1, 0, 0));		// No Margin
		TestBoxRayCastsMargin(1, FVec3(100, 100, 100), FVec3(-200, 0, 0), FVec3(1, 0, 0), 500.0f, true, 150.0f, FVec3(-50, 0, 0), FVec3(-1, 0, 0));		// Small Margin
		TestBoxRayCastsMargin(50, FVec3(100, 100, 100), FVec3(-200, 0, 0), FVec3(1, 0, 0), 500.0f, true, 150.0f, FVec3(-50, 0, 0), FVec3(-1, 0, 0));	// All margin (a sphere!)
		TestBoxRayCastsMargin(70, FVec3(100, 100, 100), FVec3(-200, 0, 0), FVec3(1, 0, 0), 500.0f, true, 130.0f, FVec3(-70, 0, 0), FVec3(-1, 0, 0));	// Too much margin (expanded sphere!)
	}

}