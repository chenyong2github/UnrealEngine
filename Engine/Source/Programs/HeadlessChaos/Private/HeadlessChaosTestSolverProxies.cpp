// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestSolverProxies.h"
#include "HeadlessChaos.h"

#include "Chaos/ParticleHandle.h"
#include "Chaos/ErrorReporter.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/Utilities.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"

#include "Modules/ModuleManager.h"
#include "Framework/PhysicsTickTask.h"


namespace ChaosTest {

    using namespace Chaos;

	template<class T>
	void SingleParticleProxySingleThreadTest()
	{
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(TVector<float, 3>(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		Chaos::FPhysicsSolver* Solver = Module->CreateSolver(true);
		Solver->SetEnabled(true);

		// Make a particle

		TUniquePtr<Chaos::TPBDRigidParticle<float, 3>> Particle = Chaos::TPBDRigidParticle<float, 3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(TVector<float, 3>(0, 0, 0));

		Solver->RegisterObject(Particle.Get());

		Particle->SetV(TVector<float, 3>(0, 0, 10));
		Solver->AddDirtyProxy(Particle->Proxy);

		Solver->PushPhysicsState(Module->GetDispatcher());

		FPhysicsSolverAdvanceTask AdvanceTask(Solver, 100.0f);
		AdvanceTask.DoTask(ENamedThreads::GameThread, FGraphEventRef());

		Solver->BufferPhysicsResults();
		Solver->FlipBuffers();
		Solver->UpdateGameThreadStructures();

		// Make sure game thread data has changed
		TVector<float, 3> V = Particle->V();
		EXPECT_EQ(V.X, 0.f);
		EXPECT_GT(V.Z, 0.f);

		TVector<float, 3> X = Particle->X();
		EXPECT_EQ(X.X, 0.f);
		EXPECT_GT(X.Z, 0.f);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);		
	}

	template void SingleParticleProxySingleThreadTest<float>();

	
	template<class T>
	void SingleParticleProxyTaskGraphTest()
	{
		//
		// DISABLED TEST
		//
		// There is currently no way to execute a TaskGraph or DedicatedThread simulation in a unit test.
		// This test should be enabled when TaskGraph simulation is supported for unit tests.
		//

		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(TVector<float, 3>(0), 10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::DedicatedThread);

		// Make a solver
		Chaos::FPhysicsSolver* Solver = Module->CreateSolver(true);
		Solver->SetEnabled(true);

		// Make a particle

		TUniquePtr<Chaos::TPBDRigidParticle<float, 3>> Particle = Chaos::TPBDRigidParticle<float, 3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(TVector<float, 3>(0, 0, 0));
		Solver->RegisterObject(Particle.Get());

		Particle->SetV(TVector<float, 3>(0, 0, 10));
		Solver->AddDirtyProxy(Particle->Proxy);

		int32 Counter = 0;
		while (Particle->X().Size() == 0.f)
		{
			Solver->PushPhysicsState(Module->GetDispatcher()); 

			// This might not be the correct way to advance when using the TaskGraph.
			FPhysicsSolverAdvanceTask AdvanceTask(Solver, 100.0f);
			AdvanceTask.DoTask(ENamedThreads::GameThread, FGraphEventRef());

			Solver->BufferPhysicsResults();
			Solver->FlipBuffers();
			Solver->UpdateGameThreadStructures();

			EXPECT_LE(Counter++, 5);
		}

		// Make sure game thread data has changed
		TVector<float, 3> V = Particle->V();
		EXPECT_EQ(V.X, 0.f);
		EXPECT_GT(V.Z, 0.f);

		TVector<float, 3> X = Particle->X();
		EXPECT_EQ(X.X, 0.f);
		EXPECT_GT(X.Z, 0.f);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	template void SingleParticleProxyTaskGraphTest<float>();
}
