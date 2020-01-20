// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"
#include "Modules/ModuleManager.h"

namespace ChaosTest {

	using namespace Chaos;

	GTEST_TEST(SimTests, SphereSphereSimTest)
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
		auto Static = Evolution.CreateStaticParticles(1)[0];
		auto Dynamic = Evolution.CreateDynamicParticles(1)[0];

		TUniquePtr<FImplicitObject> Box(new TSphere<FReal, 3>(FVec3(0, 0, 0), 50));
		Static->SetGeometry(MakeSerializable(Box));
		Dynamic->SetGeometry(MakeSerializable(Box));

		Static->X() = FVec3(10, 10, 10);
		Dynamic->X() = FVec3(10, 10, 300);
		Dynamic->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);

		const FReal Dt = 1 / 60.f;
		for (int i = 0; i < 100; ++i)
		{
			Evolution.AdvanceOneTimeStep(1 / 60.f);
			Evolution.EndFrame(Dt);
		}

		EXPECT_NEAR(Dynamic->X().Z, 110, 1);
	}

	GTEST_TEST(SimTests, BoxBoxSimTest)
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
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
	GTEST_TEST(SimTests, DISABLED_VeryLowInertiaSimTest)
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
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

		for (int i = 0; i < 100; ++i)
		{
			Evolution.AdvanceOneTimeStep(1 / 60.f);
			Evolution.EndFrame(1 / 60.f);
		}

		EXPECT_NEAR(Dynamic->X().Z, 110, 10);
	}

	GTEST_TEST(SimTests, SleepAndWakeSimTest)
	{
		TPBDRigidsSOAs<FReal, 3> Particles;
		TPBDRigidsEvolutionGBF<FReal, 3> Evolution(Particles);
		auto Static = Evolution.CreateStaticParticles(1)[0];
		auto Dynamic1 = Evolution.CreateDynamicParticles(1)[0];
		auto Dynamic2 = Evolution.CreateDynamicParticles(1)[0];

		TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;

		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);

		TUniquePtr<FChaosPhysicsMaterial> PhysicsMaterial = MakeUnique<FChaosPhysicsMaterial>();
		PhysicsMaterial->SleepingLinearThreshold = 20;
		PhysicsMaterial->SleepingAngularThreshold = 20;

		TUniquePtr<FImplicitObject> StaticBox(new TBox<FReal, 3>(FVec3(-500, -500, -50), FVec3(500, 500, 50)));
		TUniquePtr<FImplicitObject> DynamicBox(new TBox<FReal, 3>(FVec3(-50, -50, -50), FVec3(50, 50, 50)));
		Static->SetGeometry(MakeSerializable(StaticBox));
		Dynamic1->SetGeometry(MakeSerializable(DynamicBox));
		Dynamic2->SetGeometry(MakeSerializable(DynamicBox));

		Evolution.SetPhysicsMaterial(Dynamic1, MakeSerializable(PhysicsMaterial));
		Evolution.SetPhysicsMaterial(Dynamic2, MakeSerializable(PhysicsMaterial));

		Static->X() = FVec3(10, 10, 10);
		Dynamic1->X() = FVec3(10, 10, 120);
		Dynamic2->X() = FVec3(10, 10, 400);
		Dynamic1->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic1->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);
		Dynamic2->I() = FMatrix33(100000.0f, 100000.0f, 100000.0f);
		Dynamic2->InvI() = FMatrix33(1.0f / 100000.0f, 1.0f / 100000.0f, 1.0f / 100000.0f);

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

