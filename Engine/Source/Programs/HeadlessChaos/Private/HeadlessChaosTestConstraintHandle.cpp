// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidSpringConstraints.h"
#include "Chaos/PBDRigidDynamicSpringConstraints.h"

namespace ChaosTest {

	using namespace Chaos;

	/**
	 * Check that we can access and remove constraints using handles
	 */
	template<typename TConstraints>
	void CheckConstraintHandles(TConstraints& Constraints, TArray<TPBDRigidParticleHandle<FReal, 3>*> ParticleHandles, TArray<typename TConstraints::FConstraintContainerHandle*> ConstraintsHandles)
	{
		// Constraints are created in valid state
		EXPECT_EQ(Constraints.NumConstraints(), 4);
		EXPECT_TRUE(ConstraintsHandles[0]->IsValid());
		EXPECT_TRUE(ConstraintsHandles[1]->IsValid());
		EXPECT_TRUE(ConstraintsHandles[2]->IsValid());
		EXPECT_TRUE(ConstraintsHandles[3]->IsValid());

		// Can access constraints' particles
		EXPECT_EQ(ConstraintsHandles[0]->GetConstrainedParticles()[0], ParticleHandles[0]);
		EXPECT_EQ(ConstraintsHandles[1]->GetConstrainedParticles()[0], ParticleHandles[1]);
		EXPECT_EQ(ConstraintsHandles[2]->GetConstrainedParticles()[0], ParticleHandles[2]);
		EXPECT_EQ(ConstraintsHandles[3]->GetConstrainedParticles()[0], ParticleHandles[3]);

		// Some constraints are single-particle, so can't do this check for all TConstraint
		//EXPECT_EQ(ConstraintsHandles[0]->GetConstrainedParticles()[1], ParticleHandles[1]);
		//EXPECT_EQ(ConstraintsHandles[1]->GetConstrainedParticles()[1], ParticleHandles[2]);
		//EXPECT_EQ(ConstraintsHandles[2]->GetConstrainedParticles()[1], ParticleHandles[3]);
		//EXPECT_EQ(ConstraintsHandles[3]->GetConstrainedParticles()[1], ParticleHandles[4]);

		// Array is packed as expected
		EXPECT_EQ(ConstraintsHandles[0]->GetConstraintIndex(), 0);
		EXPECT_EQ(ConstraintsHandles[1]->GetConstraintIndex(), 1);
		EXPECT_EQ(ConstraintsHandles[2]->GetConstraintIndex(), 2);
		EXPECT_EQ(ConstraintsHandles[3]->GetConstraintIndex(), 3);

		// Can remove constraints (from the middle of the constraint array)
		ConstraintsHandles[1]->RemoveConstraint();
		ConstraintsHandles[1] = nullptr;
		EXPECT_EQ(Constraints.NumConstraints(), 3);

		// Array is still packed as expected
		EXPECT_EQ(ConstraintsHandles[0]->GetConstraintIndex(), 0);
		EXPECT_EQ(ConstraintsHandles[2]->GetConstraintIndex(), 2);
		EXPECT_EQ(ConstraintsHandles[3]->GetConstraintIndex(), 1);

		// Can remove constraints (from the end of the constraint array)
		ConstraintsHandles[3]->RemoveConstraint();
		ConstraintsHandles[3] = nullptr;
		EXPECT_EQ(Constraints.NumConstraints(), 2);

		// Array is still packed as expected
		EXPECT_EQ(ConstraintsHandles[0]->GetConstraintIndex(), 0);
		EXPECT_EQ(ConstraintsHandles[2]->GetConstraintIndex(), 1);

		// Can remove constraints (from beginning of the constraint array)
		ConstraintsHandles[0]->RemoveConstraint();
		ConstraintsHandles[0] = nullptr;
		EXPECT_EQ(Constraints.NumConstraints(), 1);
		EXPECT_EQ(ConstraintsHandles[2]->GetConstraintIndex(), 0);

		// Can remove last constraint
		ConstraintsHandles[2]->RemoveConstraint();
		ConstraintsHandles[2] = nullptr;
		EXPECT_EQ(Constraints.NumConstraints(), 0);
	}

	template<typename TEvolution>
	void CollisionConstraintHandles()
	{
#if CHAOS_CONSTRAINTHANDLE_TODO
		// @todo(ccaulfield): Collision Constraints Container can't be used without collision detection loop.
		TPBDRigidsSOAs<FReal, 3> Particles;
		TPBDCollisionConstraints<T, 3> Constraints;
		TEvolution Evolution(Particles);

		TArray<TPBDRigidParticleHandle<T, 3>*> ParticleHandles = Evolution.CreateDynamicParticles(5);

		TArray<TPBDCollisionConstraintHandle<T, 3>*> ConstraintsHandles =
		{
			Constraints.AddConstraint({ ParticleHandles[0], ParticleHandles[1] }, ...),
			Constraints.AddConstraint({ ParticleHandles[1], ParticleHandles[2] }, ...),
			Constraints.AddConstraint({ ParticleHandles[2], ParticleHandles[3] }, ...),
			Constraints.AddConstraint({ ParticleHandles[3], ParticleHandles[4] }, ...),
		};

		//CheckConstraintHandles(Constraints, ParticleHandles, ConstraintsHandles);
#endif
	}

	template<typename TEvolution>
	void JointConstraintHandles()
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TEvolution Evolution(Particles);

		TArray<TPBDRigidParticleHandle<FReal, 3>*> ParticleHandles = Evolution.CreateDynamicParticles(5);

		FPBDJointConstraints Constraints;
		TArray<FPBDJointConstraintHandle*> ConstraintsHandles =
		{
			Constraints.AddConstraint({ ParticleHandles[0], ParticleHandles[1] }, { TRigidTransform<FReal, 3>(), TRigidTransform<FReal, 3>() }),
			Constraints.AddConstraint({ ParticleHandles[1], ParticleHandles[2] }, { TRigidTransform<FReal, 3>(), TRigidTransform<FReal, 3>() }),
			Constraints.AddConstraint({ ParticleHandles[2], ParticleHandles[3] }, { TRigidTransform<FReal, 3>(), TRigidTransform<FReal, 3>() }),
			Constraints.AddConstraint({ ParticleHandles[3], ParticleHandles[4] }, { TRigidTransform<FReal, 3>(), TRigidTransform<FReal, 3>() }),
		};

		CheckConstraintHandles(Constraints, ParticleHandles, ConstraintsHandles);
	}

	template<typename TEvolution>
	void PositionConstraintHandles()
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TEvolution Evolution(Particles);
		TArray<TPBDRigidParticleHandle<FReal, 3>*> ParticleHandles = Evolution.CreateDynamicParticles(5);

		TPBDPositionConstraints<FReal, 3> Constraints;
		TArray<TPBDPositionConstraintHandle<FReal, 3>*> ConstraintsHandles =
		{
			Constraints.AddConstraint(ParticleHandles[0], { 0, 0, 0 }),
			Constraints.AddConstraint(ParticleHandles[1], { 0, 0, 0 }),
			Constraints.AddConstraint(ParticleHandles[2], { 0, 0, 0 }),
			Constraints.AddConstraint(ParticleHandles[3], { 0, 0, 0 }),
		};

		CheckConstraintHandles(Constraints, ParticleHandles, ConstraintsHandles);
	}

	template<typename TEvolution>
	void RigidSpringConstraintHandles()
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TEvolution Evolution(Particles);
		TArray<TPBDRigidParticleHandle<FReal, 3>*> ParticleHandles = Evolution.CreateDynamicParticles(5);

		TPBDRigidSpringConstraints<FReal, 3> Constraints;
		TArray<TPBDRigidSpringConstraintHandle<FReal, 3>*> ConstraintsHandles =
		{
			Constraints.AddConstraint({ ParticleHandles[0], ParticleHandles[1] }, { { 0, 0, 0 }, { 0, 0, 0 } }),
			Constraints.AddConstraint({ ParticleHandles[1], ParticleHandles[2] }, { { 0, 0, 0 }, { 0, 0, 0 } }),
			Constraints.AddConstraint({ ParticleHandles[2], ParticleHandles[3] }, { { 0, 0, 0 }, { 0, 0, 0 } }),
			Constraints.AddConstraint({ ParticleHandles[3], ParticleHandles[4] }, { { 0, 0, 0 }, { 0, 0, 0 } }),
		};

		CheckConstraintHandles(Constraints, ParticleHandles, ConstraintsHandles);
	}


	template<typename TEvolution>
	void RigidDynamicSpringConstraintHandles()
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TEvolution Evolution(Particles);
		TArray<TPBDRigidParticleHandle<FReal, 3>*> ParticleHandles = Evolution.CreateDynamicParticles(5);

		TPBDRigidDynamicSpringConstraints<FReal, 3> Constraints;
		TArray<TPBDRigidDynamicSpringConstraintHandle<FReal, 3>*> ConstraintsHandles =
		{
			Constraints.AddConstraint({ ParticleHandles[0], ParticleHandles[1] }),
			Constraints.AddConstraint({ ParticleHandles[1], ParticleHandles[2] }),
			Constraints.AddConstraint({ ParticleHandles[2], ParticleHandles[3] }),
			Constraints.AddConstraint({ ParticleHandles[3], ParticleHandles[4] }),
		};

		CheckConstraintHandles(Constraints, ParticleHandles, ConstraintsHandles);
	}

	TEST(ConstraintHandleTests, DISABLED_CollisionConstraintHandle)
	{
		CollisionConstraintHandles<Chaos::TPBDRigidsEvolutionGBF<FReal, 3>>();

		SUCCEED();
	}

	TEST(ConstraintHandleTests, JointConstraintHandle)
	{
		JointConstraintHandles<Chaos::TPBDRigidsEvolutionGBF<FReal, 3>>();

		SUCCEED();
	}

	TEST(ConstraintHandleTests, PositionConstraintHandles)
	{
		PositionConstraintHandles<Chaos::TPBDRigidsEvolutionGBF<FReal, 3>>();

		SUCCEED();
	}

	TEST(ConstraintHandleTests, RigidSpringConstraintHandles)
	{
		RigidSpringConstraintHandles<Chaos::TPBDRigidsEvolutionGBF<FReal, 3>>();

		SUCCEED();
	}

	TEST(ConstraintHandleTests, RigidDynamicSpringConstraintHandles)
	{
		RigidDynamicSpringConstraintHandles<Chaos::TPBDRigidsEvolutionGBF<FReal, 3>>();

		SUCCEED();
	}
}