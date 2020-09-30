// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestCollisions.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosCollisionConstraints.h"
#include "Chaos/GJK.h"
#include "Chaos/Pair.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"
#include "Modules/ModuleManager.h"


namespace ChaosTest {

	using namespace Chaos;

	// Two boxes that are just touching. Run collision detection with ShapePadding and verify
	// that the collision detection returns a depth equal to the shape padding.
	void TestBoxBoxShapePadding()
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

		auto Box0 = AppendDynamicParticleBox<FReal>(Particles, FVec3(100));
		Box0->X() = FVec3(0, 0, 0);
		Box0->R() = FRotation3(FQuat::Identity);
		Box0->V() = FVec3(0);
		Box0->PreV() = Box0->V();
		Box0->P() = Box0->X();
		Box0->Q() = Box0->R();
		Box0->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		auto Box1 = AppendDynamicParticleBox<FReal>(Particles, FVec3(100));
		Box1->X() = FVec3(0, 100, 0);
		Box1->R() = FRotation3(FQuat::Identity);
		Box1->V() = FVec3(0);
		Box1->PreV() = Box1->V();
		Box1->P() = Box1->X();
		Box1->Q() = Box1->R();
		Box1->AuxilaryValue(PhysicsMaterials) = MakeSerializable(PhysicsMaterial);

		FRigidBodyPointContactConstraint Constraint(
			Box0,
			Box0->Geometry().Get(),
			nullptr,
			FRigidTransform3(),
			Box1,
			Box1->Geometry().Get(),
			nullptr,
			FRigidTransform3(),
			EContactShapesType::BoxBox);

		{
			FReal Padding = 0.0f;
			Collisions::Update(Constraint, 1.0f + 10.0f * Padding, Padding);
			EXPECT_NEAR(Constraint.Manifold.Phi, -Padding, KINDA_SMALL_NUMBER);
		}

		{
			FReal Padding = 0.1f;
			Collisions::Update(Constraint, 10.0f * Padding, Padding);
			EXPECT_NEAR(Constraint.Manifold.Phi, -Padding, KINDA_SMALL_NUMBER);
		}

		{
			FReal Padding = 2.0f;
			Collisions::Update(Constraint, 10.0f * Padding, Padding);
			EXPECT_NEAR(Constraint.Manifold.Phi, -Padding, KINDA_SMALL_NUMBER);
		}
	}

	TEST(CollisionTests, TestBoxBoxShapePadding) {
		TestBoxBoxShapePadding();
	}

}