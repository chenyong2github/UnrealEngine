// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestConstraints.h"

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Modules/ModuleManager.h"
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

namespace ChaosTest {

	using namespace Chaos;

	/**
	 * Position constraint test
	 */
	template<typename TEvolution>
	void Position()
	{
		{
			TPBDRigidsSOAs<FReal, 3> Particles;
			TEvolution Evolution(Particles);
			TArray<TPBDRigidParticleHandle<FReal, 3>*> Dynamics = Evolution.CreateDynamicParticles(1);
			TArray<FVec3> Positions = { FVec3(0) };
			TPBDPositionConstraints<FReal, 3> PositionConstraints(MoveTemp(Positions), MoveTemp(Dynamics), 1.f);
			auto ConstraintRule = TPBDConstraintIslandRule<TPBDPositionConstraints<FReal, 3>>(PositionConstraints);
			
			Evolution.AddConstraintRule(&ConstraintRule);
			Evolution.AdvanceOneTimeStep(0.1);
			Evolution.EndFrame(0.1);
			EXPECT_LT(Evolution.GetParticleHandles().Handle(0)->X().SizeSquared(), SMALL_NUMBER);
		}
		{
			TPBDRigidsSOAs<FReal, 3> Particles;
			TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
			TArray<TPBDRigidParticleHandle<FReal, 3>*> Dynamics = Evolution.CreateDynamicParticles(1);
			Evolution.GetGravityForces().SetEnabled(*Dynamics[0], false);

			TArray<FVec3> Positions = { FVec3(1) };
			TPBDPositionConstraints<FReal, 3> PositionConstraints(MoveTemp(Positions), MoveTemp(Dynamics), 0.5f);
			auto ConstraintRule = TPBDConstraintIslandRule<TPBDPositionConstraints<FReal, 3>>(PositionConstraints);
			Evolution.AddConstraintRule(&ConstraintRule);

			Evolution.AdvanceOneTimeStep(0.1);
			Evolution.EndFrame(0.1);
			auto& Handle = Evolution.GetParticleHandles().Handle(0);
			EXPECT_LT(FMath::Abs(Handle->X()[0] - 0.5), (FReal)SMALL_NUMBER);
			EXPECT_LT(FMath::Abs(Handle->X()[1] - 0.5), (FReal)SMALL_NUMBER);
			EXPECT_LT(FMath::Abs(Handle->X()[2] - 0.5), (FReal)SMALL_NUMBER);

			Evolution.AdvanceOneTimeStep(0.1);
			Evolution.EndFrame(0.1);

			EXPECT_LT(FMath::Abs(Handle->X()[0] - 1), (FReal)SMALL_NUMBER);
			EXPECT_LT(FMath::Abs(Handle->X()[1] - 1), (FReal)SMALL_NUMBER);
			EXPECT_LT(FMath::Abs(Handle->X()[2] - 1), (FReal)SMALL_NUMBER);
		}
	}


	/**
	 * Joint constraints test with the fixed body held in place with a position constraint.
	 * Joint body swings under the fixed body at fixed distance.
	 */
	template<typename TEvolution>
	void PositionAndJoint()
	{
		const int32 Iterations = 10;
		TPBDRigidsSOAs<FReal, 3> Particles;
		TEvolution Evolution(Particles, Iterations);
		TArray<TPBDRigidParticleHandle<FReal, 3>*> Dynamics = Evolution.CreateDynamicParticles(2);
		TArray<FVec3> PositionConstraintPositions = { FVec3(0, 0, 0) };

		Dynamics[1]->X() = FVec3(500, 0, 0);
		FVec3 JointConstraintPosition = FVec3(0, 0, 0);

		TArray<TPBDRigidParticleHandle<FReal, 3>*> PositionParticles = { Dynamics[0] };
		TPBDPositionConstraints<FReal, 3> PositionConstraints(MoveTemp(PositionConstraintPositions), MoveTemp(PositionParticles), 1.f);
		auto PositionConstraintRule = TPBDConstraintIslandRule<TPBDPositionConstraints<FReal, 3>>(PositionConstraints);
		Evolution.AddConstraintRule(&PositionConstraintRule);

		TVector<TGeometryParticleHandle<FReal, 3>*, 2> JointParticles = { Dynamics[0], Dynamics[1] };
		FPBDJointConstraints JointConstraints;
		JointConstraints.AddConstraint(JointParticles, FRigidTransform3(JointConstraintPosition, FRotation3::FromIdentity()));
		auto JointConstraintRule = TPBDConstraintIslandRule<FPBDJointConstraints>(JointConstraints);
		Evolution.AddConstraintRule(&JointConstraintRule);

		FReal Dt = 0.1f;
		for (int32 TimeIndex = 0; TimeIndex < 100; ++TimeIndex)
		{
			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);

			const auto& Pos0 = Dynamics[0]->X();
			const auto& Pos1 = Dynamics[1]->X();
			const float Delta0 = (Pos0 - FVec3(0, 0, 0)).Size();
			const float Separation = (Pos1 - Pos0).Size();

			EXPECT_LT(Delta0, 5);
			EXPECT_GT(Separation, 495);
			EXPECT_LT(Separation, 505);
		}
	}


	TEST(ConstraintTests, Constraints)
	{
		ChaosTest::Position<Chaos::TPBDRigidsEvolutionGBF<float, 3>>();
		ChaosTest::PositionAndJoint<Chaos::TPBDRigidsEvolutionGBF<float, 3>>();

#if CHAOS_PARTICLEHANDLE_TODO
		ChaosTest::Position<Chaos::TPBDRigidsEvolutionPGS<float, 3>, float>();
		ChaosTest::PositionAndJoint<Chaos::TPBDRigidsEvolutionPGS<float, 3>>();
#endif
		SUCCEED();
	}

}