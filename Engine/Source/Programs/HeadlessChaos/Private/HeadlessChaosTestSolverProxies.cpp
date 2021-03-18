// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestSolverProxies.h"
#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"


#include "Chaos/ParticleHandle.h"
#include "Chaos/ErrorReporter.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/Utilities.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"

#include "Modules/ModuleManager.h"


namespace ChaosTest {

	using namespace Chaos;

	void SingleParticleProxySingleThreadTest()
	{
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<FReal, 3>(FVec3(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver(nullptr);

		// Make a particle


		auto Proxy = FSingleParticlePhysicsProxy::Create(Chaos::FPBDRigidParticle::CreateParticle());
		auto& Particle = Proxy->GetGameThreadAPI();
		Particle.SetGeometry(Sphere);
		Particle.SetX(FVec3(0, 0, 0));
		Particle.SetGravityEnabled(false);
		Solver->RegisterObject(Proxy);

		Particle.SetV(FVec3(0, 0, 10));

		::ChaosTest::SetParticleSimDataToCollide({ Proxy->GetParticle_LowLevel() });

		Solver->AdvanceAndDispatch_External(100.0f);
		Solver->UpdateGameThreadStructures();

		// Make sure game thread data has changed
		FVec3 V = Particle.V();
		EXPECT_EQ(V.X, 0.f);
		EXPECT_GT(V.Z, 0.f);

		FVec3 X = Particle.X();
		EXPECT_EQ(X.X, 0.f);
		EXPECT_GT(X.Z, 0.f);

		// Throw out the proxy
		Solver->UnregisterObject(Proxy);

		Module->DestroySolver(Solver);
	}

	void SingleParticleProxyWakeEventPropagationTest()
	{
		using namespace Chaos;
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<FReal, 3>(FVec3(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver(nullptr);

		// Make a particle

		auto Proxy = FSingleParticlePhysicsProxy::Create(Chaos::FPBDRigidParticle::CreateParticle());
		auto& Particle = Proxy->GetGameThreadAPI();
		Particle.SetGeometry(Sphere);
		Particle.SetX(FVec3(0, 0, 220));
		Particle.SetV(FVec3(0, 0, -10));
		Particle.SetCCDEnabled(true);
		Solver->RegisterObject(Proxy);
		Solver->AddDirtyProxy(Proxy);

		auto Proxy2 = FSingleParticlePhysicsProxy::Create(Chaos::FPBDRigidParticle::CreateParticle());
		auto& Particle2 = Proxy2->GetGameThreadAPI();
		Particle2.SetGeometry(Sphere);
		Particle2.SetX(FVec3(0, 0, 100));
		Particle2.SetV(FVec3(0, 0, 0));
		Solver->RegisterObject(Proxy2);
		Particle2.SetObjectState(Chaos::EObjectStateType::Sleeping);

		::ChaosTest::SetParticleSimDataToCollide({ Proxy->GetParticle_LowLevel(),Proxy2->GetParticle_LowLevel() });

		// let top paticle collide and wake up second particle
		int32 LoopCount = 0;
		while (Particle2.GetWakeEvent() == EWakeEventEntry::None && LoopCount++ < 20)
		{
			Solver->AdvanceAndDispatch_External(100.0f);
			Solver->UpdateGameThreadStructures();
		}

		// Make sure game thread data has changed
		FVec3 V = Particle.V();
		EXPECT_EQ(Particle.GetWakeEvent(), EWakeEventEntry::None);
		EXPECT_EQ(Particle.ObjectState(), Chaos::EObjectStateType::Dynamic);

		EXPECT_EQ(Particle2.GetWakeEvent(), EWakeEventEntry::Awake);
		EXPECT_EQ(Particle2.ObjectState(), Chaos::EObjectStateType::Dynamic);

		Particle2.ClearEvents();
		EXPECT_EQ(Particle2.GetWakeEvent(), EWakeEventEntry::None);

		// Throw out the proxy
		Solver->UnregisterObject(Proxy);

		Module->DestroySolver(Solver);
	}

	GTEST_TEST(AllTraits, SingleParticleProxyTests)
	{
		ChaosTest::SingleParticleProxySingleThreadTest();
		ChaosTest::SingleParticleProxyWakeEventPropagationTest();
	}
}
