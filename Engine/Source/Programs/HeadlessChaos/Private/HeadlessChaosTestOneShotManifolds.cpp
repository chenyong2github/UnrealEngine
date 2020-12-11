// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "Chaos/Capsule.h"
#include "Chaos/Convex.h"
#include "Chaos/ImplicitObjectScaled.h"
//#include "Chaos/ImplicitFwd.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
//#include "Chaos/CollisionResolution.h"

//PRAGMA_DISABLE_OPTIMIZATION


namespace Chaos
{
	namespace Collisions
	{
		// Forward declaration of functions that we need to test but is not part of the public interface
		void ConstructBoxBoxOneShotManifold(
			const Chaos::FImplicitBox3& Box1,
			const Chaos::FRigidTransform3& Box1Transform, //world
			const Chaos::FImplicitBox3& Box2,
			const Chaos::FRigidTransform3& Box2Transform, //world
			const Chaos::FReal CullDistance,
			const Chaos::FReal Dt,
			Chaos::FRigidBodyPointContactConstraint& Constraint);
	}
}


namespace ChaosTest
{
	using namespace Chaos;

	

	TEST(OneShotManifoldTest, Boxes)
	{
		FReal Dt = 1 / 30.0f;
		// Test 1 is a degenerate case where 2 boxes are on top of each other. Make sure that it does not crash
		{
			TBox<FReal, 3> Box1(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			TBox<FReal, 3> Box2(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			FRigidTransform3 Box1Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 0.0f), FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));
			FRigidTransform3 Box2Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 0.0f), FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));

			FRigidBodyPointContactConstraint Constraint;
			FReal CullingDistance = 100.0f;

			Collisions::ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, CullingDistance, Dt, Constraint);

			// Result should give a negative phi on all contacts
			// Phi direction may be in a random face direction
			int ContactCount = Constraint.GetManifoldPoints().Num();
			for (int ConstraintIndex = 0; ConstraintIndex < ContactCount; ConstraintIndex++)
			{
				EXPECT_NEAR(-200.0f, Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Phi, 0.01);
				EXPECT_NEAR(FMath::Abs(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.X), 100.0f, 0.01);
				EXPECT_NEAR(FMath::Abs(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Y), 100.0f, 0.01);
				EXPECT_NEAR(FMath::Abs(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Z), 100.0f, 0.01);
			}
		}

		// Test 2 Very simple case of one box on top of another (slightly separated)
		{
			TBox<FReal, 3> Box1(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			TBox<FReal, 3> Box2(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			FRigidTransform3 Box1Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 210.0f), FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));
			FRigidTransform3 Box2Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 0.0f), FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));

			FRigidBodyPointContactConstraint Constraint;
			FReal CullingDistance = 100.0f;

			Collisions::ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, CullingDistance, Dt, Constraint);
			int ContactCount = Constraint.GetManifoldPoints().Num();
			EXPECT_EQ(ContactCount, 4);
			for (int ConstraintIndex = 0; ConstraintIndex < ContactCount; ConstraintIndex++)
			{
				EXPECT_NEAR(10.0f, Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Phi, 0.01);
				EXPECT_NEAR(FMath::Abs(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.X),100.0f ,0.01);
				EXPECT_NEAR(FMath::Abs(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Y), 100.0f, 0.01);
				EXPECT_NEAR(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Z, 100.0f, 0.01);
			}
		}

		// Test 2b Same as test 1b, but rotate box 2 a bit so that box1 is the reference cube
		{
			TBox<FReal, 3> Box1(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			TBox<FReal, 3> Box2(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			FRigidTransform3 Box1Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 210.0f), FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));
			FRigidTransform3 Box2Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 0.0f), FRotation3::FromAxisAngle(FVec3(0.0f, 1.0f, 0.0f), 0.1f));

			FRigidBodyPointContactConstraint Constraint;
			FReal CullingDistance = 100.0f;

			Collisions::ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, CullingDistance, Dt, Constraint);
			int ContactCount = Constraint.GetManifoldPoints().Num();
			EXPECT_EQ(ContactCount, 4);
			for (int ConstraintIndex = 0; ConstraintIndex < ContactCount; ConstraintIndex++)
			{
				EXPECT_NEAR(10.0f, Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Phi, 15.0f); // The cube is at an angle now
				EXPECT_NEAR(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Z, 110.0f, 15.0f);
			}
		}

		// Test 3 one box on top of another (slightly separated)
		// The box vertices are offset
		{
			FVec3 OffsetBox1(300.0f, 140.0f, -210.0f);
			FVec3 OffsetBox2(-300.0f, 20.0f, 30.0f);

			TBox<FReal, 3> Box1(FVec3(-100.0f, -100, -100.0f) + OffsetBox1, FVec3(100.0f, 100.0f, 100.0f) + OffsetBox1);
			TBox<FReal, 3> Box2(FVec3(-100.0f, -100, -100.0f) + OffsetBox2, FVec3(100.0f, 100.0f, 100.0f) + OffsetBox2);
			FRigidTransform3 Box1Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 210.0f) - OffsetBox1, FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));
			FRigidTransform3 Box2Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 0.0f) - OffsetBox2, FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));

			FRigidBodyPointContactConstraint Constraint;
			FReal CullingDistance = 100.0f;

			Collisions::ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, CullingDistance, Dt, Constraint);
			int ContactCount = Constraint.GetManifoldPoints().Num();
			EXPECT_EQ(ContactCount, 4);
			for (int ConstraintIndex = 0; ConstraintIndex < ContactCount; ConstraintIndex++)
			{
				EXPECT_NEAR(10.0f, Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Phi, 0.01);
				EXPECT_NEAR(FMath::Abs(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.X), 100.0f, 0.01);
				EXPECT_NEAR(FMath::Abs(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Y), 100.0f, 0.01);
				EXPECT_NEAR(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Z, 100.0f, 0.01);
			}
		}

		// Test 4 one box on top of another (slightly separated)
		// With transforms
		{
			// Vertex offsets
			FVec3 OffsetBox1(300.0f, 140.0f, -210.0f);
			FVec3 OffsetBox2(-300.0f, 20.0f, 30.0f);

			FVec3 Axis(1.0f, 1.0f, 1.0f) ;
			Axis.Normalize();
			ensure(Axis.IsNormalized());

			FRigidTransform3 RotationTransform(FVec3(0.0f, 0.0f, 0.0f), FRotation3::FromAxisAngle(Axis, PI/2));

			FRigidTransform3 TranslationTransform1(FVec3(-100.0f, 50.0f, 1000.0f + 210.0f) - OffsetBox1, FRotation3::FromAxisAngle(FVec3(1.0f, 0.0f, 0.0f), 0));
			FRigidTransform3 TranslationTransform2(FVec3(-100.0f, 50.0f, 1000.0f + 0.0) - OffsetBox2, FRotation3::FromAxisAngle(FVec3(0.0f, 1.0f, 0.0f), 0));

			TBox<FReal, 3> Box1(FVec3(-100.0f, -100, -100.0f) + OffsetBox1, FVec3(100.0f, 100.0f, 100.0f) + OffsetBox1);
			TBox<FReal, 3> Box2(FVec3(-100.0f, -100, -100.0f) + OffsetBox2, FVec3(100.0f, 100.0f, 100.0f) + OffsetBox2);

			FRigidTransform3 Box1Transform = TranslationTransform1 * RotationTransform;
			FRigidTransform3 Box2Transform = TranslationTransform2 * RotationTransform;

			FRigidBodyPointContactConstraint Constraint;
			FReal CullingDistance = 100.0f;

			Collisions::ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, CullingDistance, Dt, Constraint);
			int ContactCount = Constraint.GetManifoldPoints().Num();
			EXPECT_EQ(ContactCount, 4);
			for (int ConstraintIndex = 0; ConstraintIndex < ContactCount; ConstraintIndex++)
			{
				EXPECT_NEAR(10.0f, Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Phi, 0.01);
				FVec3 Location = Box2Transform.InverseTransformPosition( Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location) - OffsetBox2;
				EXPECT_NEAR(FMath::Abs(Location.X), 100.0f, 0.01);
				EXPECT_NEAR(FMath::Abs(Location.Y), 100.0f, 0.01);
				EXPECT_NEAR(Location.Z, 100.0f, 0.01);
			}
		}

		// Test 5 one box on top of another (slightly separated)
		// With 90 degree box rotations
		{
			// Vertex offsets
			FVec3 OffsetBox1(0.0f, 210.0f, 0.0f); // Will rotate into z
			FVec3 OffsetBox2(0.0f, 0.0f, 0.0f);

			FVec3 Axis(1.0f, 1.0f, 1.0f);
			Axis.Normalize();
			ensure(Axis.IsNormalized());

			//FRigidTransform3 RotationTransform(FVec3(0.0f, 0.0f, 0.0f), FRotation3::FromAxisAngle(Axis, PI / 2));

			FRigidTransform3 Box1Transform(FVec3(0.0f, 0.0f, 0.0f /* 210.0f*/), FRotation3::FromAxisAngle(FVec3(1.0f, 0.0f, 0.0f), PI / 2));
			FRigidTransform3 Box2Transform(FVec3(0.0, 0.0f, 0.0f + 0.0), FRotation3::FromAxisAngle(FVec3(0.0f, 1.0f, 0.0f), PI / 2));

			TBox<FReal, 3> Box1(FVec3(-100.0f, -100, -100.0f) + OffsetBox1, FVec3(100.0f, 100.0f, 100.0f) + OffsetBox1);
			TBox<FReal, 3> Box2(FVec3(-100.0f, -100, -100.0f) + OffsetBox2, FVec3(100.0f, 100.0f, 100.0f) + OffsetBox2);

			FRigidBodyPointContactConstraint Constraint;
			FReal CullingDistance = 100.0f;

			Collisions::ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, CullingDistance, Dt, Constraint);
			int ContactCount = Constraint.GetManifoldPoints().Num();
			for (int ConstraintIndex = 0; ConstraintIndex < ContactCount; ConstraintIndex++)
			{
				EXPECT_NEAR(10.0f, Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Phi, 0.01);
				FVec3 Location = Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location;
				EXPECT_NEAR(Location.Z, 100.0f, 0.01);
			}
		}

		// Test 6 Rotate top box by 45 degrees
		{
			TBox<FReal, 3> Box1(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			TBox<FReal, 3> Box2(FVec3(-100.0f, -100, -100.0f), FVec3(100.0f, 100.0f, 100.0f));
			FRigidTransform3 Box1Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 210.0f), FRotation3::FromAxisAngle(FVec3(0.0f, 0.0f, 1.0f), PI / 2));
			FRigidTransform3 Box2Transform = FRigidTransform3(FVec3(0.0f, 0.0f, 0.0f), FRotation3::FromElements(0.0f, 0.0f, 0.0f, 1.0f));

			FRigidBodyPointContactConstraint Constraint;
			FReal CullingDistance = 100.0f;

			Collisions::ConstructBoxBoxOneShotManifold(Box1, Box1Transform, Box2, Box2Transform, CullingDistance, Dt, Constraint);
			int ContactCount = Constraint.GetManifoldPoints().Num();
			EXPECT_EQ(ContactCount, 4);  
			for (int ConstraintIndex = 0; ConstraintIndex < ContactCount; ConstraintIndex++)
			{
				EXPECT_NEAR(10.0f, Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Phi, 0.01);
				EXPECT_NEAR(Constraint.GetManifoldPoints()[ConstraintIndex].ContactPoint.Location.Z, 100.0f, 0.01);
			}
		}

	}

}