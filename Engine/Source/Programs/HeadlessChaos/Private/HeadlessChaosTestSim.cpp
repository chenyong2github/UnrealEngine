// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"

#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"
#include "Modules/ModuleManager.h"
#include "ChaosSolversModule.h"
#include "PBDRigidsSolver.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"

namespace ChaosTest {

	using namespace Chaos;

	GTEST_TEST(AllTraits, SimTests_SphereSphereSimTest_StaticBoundsChange)
	{
		// This test spawns a dynamic and a static, then moves the static around a few times after initialization.
		// The goal is to make sure that the bounds are updated correctly and the dynamic rests on top of the static
		// in its final position.

		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<FReal, 3>(FVec3(0), 10));

		// Create solver #TODO make FFramework a little more general instead of mostly geometry collection focused
		GeometryCollectionTest::FFramework Framework;

		// Make a particle
		auto Proxy = FSingleParticlePhysicsProxy::Create(Chaos::FPBDRigidParticle::CreateParticle());
		auto& Particle = Proxy->GetGameThreadAPI();
		Particle.SetGeometry(Sphere);
		Particle.SetX(FVec3(1000, 1000, 200));
		Particle.SetGravityEnabled(true);
		Framework.Solver->RegisterObject(Proxy);

		auto StaticProxy = FSingleParticlePhysicsProxy::Create(Chaos::FGeometryParticle::CreateParticle());
		auto& Static = StaticProxy->GetGameThreadAPI();
		Static.SetGeometry(Sphere);
		Static.SetX(FVec3(0, 0, 0));
		Framework.Solver->RegisterObject(StaticProxy);

		Static.SetX(FVec3(2000, 1000, 0));
		Static.SetX(FVec3(3000, 1000, 0));

		::ChaosTest::SetParticleSimDataToCollide({ Proxy->GetParticle_LowLevel(), StaticProxy->GetParticle_LowLevel() });

		for (int32 Iter = 0; Iter < 200; ++Iter)
		{
			Framework.Advance();

			if (Iter == 0)
			{
				Static.SetX(FVec3(1000, 1000, 0));
			}
		}

		EXPECT_NEAR(Particle.X().Z, 20, 1);
	}

	GTEST_TEST(AllEvolutions, SimTests_SphereSphereSimTest)
	{
		FPBDRigidsSOAs Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		FPBDRigidsEvolutionGBF Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		auto Static = Evolution.CreateStaticParticles(1)[0];
		auto Dynamic = Evolution.CreateDynamicParticles(1)[0];

		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepCounterThreshold = 2;

		TUniquePtr<FImplicitObject> Sphere(new TSphere<FReal, 3>(FVec3(0, 0, 0), 50));
		Static->SetGeometry(MakeSerializable(Sphere));
		Dynamic->SetGeometry(MakeSerializable(Sphere));

		Evolution.SetPhysicsMaterial(Dynamic, MakeSerializable(PhysicsMaterial));

		Static->X() = FVec3(10, 10, 10);
		Dynamic->X() = FVec3(10, 10, 150);
		Dynamic->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);

		// The position of the static has changed and statics don't automatically update bounds, so update explicitly
		Static->SetWorldSpaceInflatedBounds(Sphere->BoundingBox().TransformedAABB(FRigidTransform3(Static->X(), Static->R())));

		::ChaosTest::SetParticleSimDataToCollide({ Static,Dynamic });


		const FReal Dt = 1 / 60.f;
		for (int i = 0; i < 200; ++i)
		{
			Evolution.AdvanceOneTimeStep(1 / 60.f);
			Evolution.EndFrame(Dt);
		}

		EXPECT_NEAR(Dynamic->X().Z, 110, 1);
	}

	GTEST_TEST(AllEvolutions, SimTests_BoxBoxSimTest)
	{
		FPBDRigidsSOAs Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		FPBDRigidsEvolutionGBF Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		auto Static = Evolution.CreateStaticParticles(1)[0];
		auto Dynamic = Evolution.CreateDynamicParticles(1)[0];

		TUniquePtr<FImplicitObject> StaticBox(new TBox<FReal, 3>(FVec3(-50, -50, -50), FVec3(50, 50, 50)));
		TUniquePtr<FImplicitObject> DynamicBox(new TBox<FReal, 3>(FVec3(-50, -50, -50), FVec3(50, 50, 50)));
		Static->SetGeometry(MakeSerializable(StaticBox));
		Dynamic->SetGeometry(MakeSerializable(DynamicBox));

		Static->X() = FVec3(10, 10, 10);
		Dynamic->X() = FVec3(10, 10, 300);
		Dynamic->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);

		::ChaosTest::SetParticleSimDataToCollide({ Static,Dynamic });


		const FReal Dt = 1 / 60.f;
		for (int i = 0; i < 100; ++i)
		{
			Evolution.AdvanceOneTimeStep(Dt);
			Evolution.EndFrame(Dt);
		}

		EXPECT_NEAR(Dynamic->X().Z, 110, 5);
	}

	// This test will fail because the inertia of the dynamic box is very low. The mass and inertia are both 1.0, 
	// but the box is 100x100x100. When we detect collisions, we get points around the edge of the box. The impulse
	// required to stop the velocity at that point is tiny because a tiny impulse can impart a large angular velocity
	// at that position. Therefore we would need a very large number of iterations to resolve it.
	// 
	// This will be fixed if/when we have a multi-contact manifold between particle pairs and we simultaneously
	// resolve contacts in that manifold.
	//
	GTEST_TEST(AllEvolutions, DISABLED_SimTests_VeryLowInertiaSimTest)
	{
		FPBDRigidsSOAs Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		FPBDRigidsEvolutionGBF Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		auto Static = Evolution.CreateStaticParticles(1)[0];
		auto Dynamic = Evolution.CreateDynamicParticles(1)[0];

		TUniquePtr<FImplicitObject> StaticBox(new TBox<FReal, 3>(FVec3(-50, -50, -50), FVec3(50, 50, 50)));
		TUniquePtr<FImplicitObject> DynamicBox(new TBox<FReal, 3>(FVec3(-50, -50, -50), FVec3(50, 50, 50)));
		Static->SetGeometry(MakeSerializable(StaticBox));
		Dynamic->SetGeometry(MakeSerializable(DynamicBox));

		Static->X() = FVec3(10, 10, 10);
		Dynamic->X() = FVec3(10, 10, 300);
		Dynamic->I() = FMatrix33(1, 1, 1);
		Dynamic->InvI() = FMatrix33(1, 1, 1);

		::ChaosTest::SetParticleSimDataToCollide({ Static,Dynamic });

		for (int i = 0; i < 100; ++i)
		{
			Evolution.AdvanceOneTimeStep(1 / 60.f);
			Evolution.EndFrame(1 / 60.f);
		}

		EXPECT_NEAR(Dynamic->X().Z, 110, 10);
	}

	GTEST_TEST(AllEvolutions, SimTests_SleepAndWakeSimTest)
	{
		FPBDRigidsSOAs Particles;
		THandleArray<FChaosPhysicsMaterial> PhysicalMaterials;
		FPBDRigidsEvolutionGBF Evolution(Particles, PhysicalMaterials);
		InitEvolutionSettings(Evolution);

		auto Static = Evolution.CreateStaticParticles(1)[0];
		auto Dynamic1 = Evolution.CreateDynamicParticles(1)[0];
		auto Dynamic2 = Evolution.CreateDynamicParticles(1)[0];

		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;

		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);

		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepingLinearThreshold = 20;
		PhysicsMaterial->SleepingAngularThreshold = 20;
		PhysicsMaterial->SleepCounterThreshold = 5;

		Static->X() = FVec3(10, 10, 10);
		Dynamic1->X() = FVec3(10, 10, 120);
		Dynamic2->X() = FVec3(10, 10, 400);

		TUniquePtr<FImplicitObject> StaticBox(new TBox<FReal, 3>(FVec3(-500, -500, -50), FVec3(500, 500, 50)));
		TUniquePtr<FImplicitObject> DynamicBox(new TBox<FReal, 3>(FVec3(-50, -50, -50), FVec3(50, 50, 50)));
		Static->SetGeometry(MakeSerializable(StaticBox));
		Dynamic1->SetGeometry(MakeSerializable(DynamicBox));
		Dynamic2->SetGeometry(MakeSerializable(DynamicBox));

		Evolution.SetPhysicsMaterial(Dynamic1, MakeSerializable(PhysicsMaterial));
		Evolution.SetPhysicsMaterial(Dynamic2, MakeSerializable(PhysicsMaterial));

		Dynamic1->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic1->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);
		Dynamic2->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic2->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);

		::ChaosTest::SetParticleSimDataToCollide({ Static,Dynamic1,Dynamic2 });

		bool Dynamic1WentToSleep = false;
		bool Dynamic1HasWokeAgain = false;
		for (int i = 0; i < 1000; ++i)
		{
			Evolution.AdvanceOneTimeStep(1 / 60.f);
			Evolution.EndFrame(1 / 60.f);

			// at some point Dynamic1 should come to rest and go to sleep on static particle
			if (Dynamic1WentToSleep == false && Dynamic1->ObjectState() == EObjectStateType::Sleeping)
			{
				Dynamic1WentToSleep = true;

				EXPECT_LT(Dynamic1->X().Z, 120);
				EXPECT_GT(Dynamic1->X().Z, 100);
			}

			// later the Dynamic2 collides with Dynamic1 waking it up again
			if (Dynamic1WentToSleep)
			{
				if (Dynamic1->ObjectState() == EObjectStateType::Dynamic)
				{
					Dynamic1HasWokeAgain = true;
				}
			}
		}

		EXPECT_TRUE(Dynamic1WentToSleep);
		EXPECT_TRUE(Dynamic1HasWokeAgain);
	}
}

