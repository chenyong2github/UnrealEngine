// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"

#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidSpringConstraints.h"

#include "ChaosLog.h"

namespace ChaosTest
{
	using namespace Chaos;

	// Check thar spring constraints work with MinEvolution
	GTEST_TEST(MinEvolutionTests, TestSpringConstraints)
	{
		// @todo(ccaulfield): remove template parameters on collisions and other constraints
		using FCollisionConstraints = TPBDCollisionConstraints<FReal, 3>;
		using FCollisionDetector = TCollisionDetector<FParticlePairBroadPhase, FNarrowPhase, FSyncCollisionReceiver, FCollisionConstraints>;
		using FRigidParticleSOAs = TPBDRigidsSOAs<FReal, 3>;
		using FParticleHandle = TPBDRigidParticleHandle<FReal, 3>;
		using FParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;

		// Particles
		FRigidParticleSOAs ParticlesContainer;

		// @todo(ccaulfield): we shouldn't require collisions to use an evolution...
		// Stuff needed for collisions
		TArray<FParticlePair> ActivePotentiallyCollidingPairs;
		TArrayCollectionArray<bool> CollidedParticles;
		TArrayCollectionArray<Chaos::TSerializablePtr<Chaos::FChaosPhysicsMaterial>> ParticleMaterials;
		TArrayCollectionArray<TUniquePtr<Chaos::FChaosPhysicsMaterial>> PerParticleMaterials;
		FCollisionConstraints Collisions(ParticlesContainer, CollidedParticles, ParticleMaterials);
		FParticlePairBroadPhase BroadPhase(ActivePotentiallyCollidingPairs, 0);
		FCollisionDetector CollisionDetector(BroadPhase, Collisions);
		TSimpleConstraintRule<FCollisionConstraints> CollisionsRule(1, Collisions);
		// End collisions stuff

		// Springs
		FPBDRigidSpringConstraints Springs;
		TSimpleConstraintRule<FPBDRigidSpringConstraints> SpringsRule(0, Springs);

		// Evolution
		// @todo(ccaulfield): this should start with some reasonable default iterations
		FPBDMinEvolution Evolution(ParticlesContainer, CollisionDetector, 0);
		Evolution.SetNumIterations(1);
		Evolution.SetNumPushOutIterations(0);

		Evolution.AddConstraintRule(&SpringsRule);
		Evolution.SetGravity(FVec3(0));

		FReal Dt = 1.0f / 30.0f;

		// Add a couple dynamic particles connected by a spring
		TArray<FParticleHandle*> Particles = ParticlesContainer.CreateDynamicParticles(2);

		// Set up Particles
		// @todo(ccaulfield) this needs to be easier
		Particles[0]->X() = FVec3(-50, 0, 0);
		Particles[0]->M() = 1.0f;
		Particles[0]->I() = FMatrix33(100.0f, 100.0f, 100.0f);
		Particles[0]->InvM() = 1.0f;
		Particles[0]->InvI() = FMatrix33(1.0f / 100.0f, 1.0f / 100.0f, 1.0f / 100.0f);

		Particles[1]->X() = FVec3(50, 0, 0);
		Particles[1]->M() = 1.0f;
		Particles[1]->I() = FMatrix33(100.0f, 100.0f, 100.0f);
		Particles[1]->InvM() = 1.0f;
		Particles[1]->InvI() = FMatrix33(1.0f / 100.0f, 1.0f / 100.0f, 1.0f / 100.0f);

		// Spring connectors at particle centres
		TArray<FVec3> Locations =
		{
			FVec3(-50, 0, 0),
			FVec3(50, 0, 0)
		};

		// Create springs
		FPBDRigidSpringConstraintHandle* Spring = Springs.AddConstraint({ Particles[0], Particles[1] }, { Locations[0], Locations[1] }, 0.1f, 0.0f, 60.0f);

		for (int32 TimeIndex = 0; TimeIndex < 1000; ++TimeIndex)
		{
			//UE_LOG(LogChaos, Warning, TEXT("%d: %f %f %f - %f %f %f"), TimeIndex, Particles[0]->X().X, Particles[0]->X().Y, Particles[0]->X().Z, Particles[1]->X().X, Particles[1]->X().Y, Particles[1]->X().Z);
			Evolution.Advance(Dt, 1);
		}

		// Particles should be separated by the spring's rest length
		FVec3 P0 = Particles[0]->X();
		FVec3 P1 = Particles[1]->X();
		FReal Distance01 = (P0 - P1).Size();
		EXPECT_NEAR(Distance01, Spring->GetRestLength(), 0.1f);
	}

}