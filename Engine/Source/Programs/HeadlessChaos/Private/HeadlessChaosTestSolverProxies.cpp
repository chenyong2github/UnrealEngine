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

	template<typename Traits, class T>
	void SingleParticleProxySingleThreadTest()
	{
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(FVec3(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<Traits>(nullptr);
		
		// Make a particle

		TUniquePtr<Chaos::TPBDRigidParticle<float, 3>> Particle = Chaos::TPBDRigidParticle<float, 3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(FVec3(0, 0, 0));
		Particle->SetGravityEnabled(false);
		Solver->RegisterObject(Particle.Get());

		Particle->SetV(FVec3(0, 0, 10));
		Solver->AddDirtyProxy(Particle->GetProxy());

		::ChaosTest::SetParticleSimDataToCollide({ Particle.Get() });

		Solver->AdvanceAndDispatch_External(100.0f);
		Solver->UpdateGameThreadStructures();

		// Make sure game thread data has changed
		FVec3 V = Particle->V();
		EXPECT_EQ(V.X, 0.f);
		EXPECT_GT(V.Z, 0.f);

		FVec3 X = Particle->X();
		EXPECT_EQ(X.X, 0.f);
		EXPECT_GT(X.Z, 0.f);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);		
	}

	template<typename Traits, class T>
	void SingleParticleProxyWakeEventPropergationTest()
	{
		using namespace Chaos;
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(FVec3(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<Traits>(nullptr);
		
		// Make a particle

		TUniquePtr<TPBDRigidParticle<float, 3>> Particle = TPBDRigidParticle<float, 3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(FVec3(0, 0, 220));
		Particle->SetV(FVec3(0, 0, -10));
		Solver->RegisterObject(Particle.Get());
		Solver->AddDirtyProxy(Particle->GetProxy());

		TUniquePtr<TPBDRigidParticle<float, 3>> Particle2 = TPBDRigidParticle<float, 3>::CreateParticle();
		Particle2->SetGeometry(Sphere);
		Particle2->SetX(FVec3(0, 0, 100));
		Particle2->SetV(FVec3(0, 0, 0));
		Solver->RegisterObject(Particle2.Get());
		Solver->AddDirtyProxy(Particle2->GetProxy());
		Particle2->SetObjectState(Chaos::EObjectStateType::Sleeping);

		::ChaosTest::SetParticleSimDataToCollide({ Particle.Get(),Particle2.Get() });

		// let top paticle collide and wake up second particle
		int32 LoopCount = 0;
		while (Particle2->GetWakeEvent() == EWakeEventEntry::None && LoopCount++ < 20)
		{
			Solver->AdvanceAndDispatch_External(100.0f);
			Solver->UpdateGameThreadStructures();
		}

		// Make sure game thread data has changed
		FVec3 V = Particle->V();
		EXPECT_EQ(Particle->GetWakeEvent(), EWakeEventEntry::None);
		EXPECT_EQ(Particle->ObjectState(), Chaos::EObjectStateType::Dynamic);

		EXPECT_EQ(Particle2->GetWakeEvent(), EWakeEventEntry::Awake);
		EXPECT_EQ(Particle2->ObjectState(), Chaos::EObjectStateType::Dynamic);

		Particle2->ClearEvents();
		EXPECT_EQ(Particle2->GetWakeEvent(), EWakeEventEntry::None);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, SingleParticleProxyTests)
	{
		ChaosTest::SingleParticleProxySingleThreadTest<TypeParam,float>();
		ChaosTest::SingleParticleProxyWakeEventPropergationTest<TypeParam,float>();
	}
}
