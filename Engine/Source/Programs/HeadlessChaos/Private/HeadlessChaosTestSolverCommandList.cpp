// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestSolverCommandList.h"
#include "HeadlessChaos.h"

#include "Chaos/ParticleHandle.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Sphere.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/Utilities.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"

#include "Modules/ModuleManager.h"
#include "Framework/PhysicsTickTask.h"
#include "Framework/CommandBuffer.h"


namespace ChaosTest {

    using namespace Chaos;

	template<class T>
	void CommandListTest()
	{
		auto Sphere = TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(new TSphere<float, 3>(TVector<float, 3>(0), 10));
		typedef Chaos::TPBDRigidParticle<T, 3> FRigidParticle;
		typedef TUniquePtr<FRigidParticle> FRigidParticlePtr;

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::DedicatedThread);

		// Make a solver
		Chaos::FPhysicsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetEnabled(true);

		// Make a particle
		FRigidParticlePtr Particle = TPBDRigidParticle<T,3>::CreateParticle();
		Particle->SetGeometry(Sphere);
		Particle->SetX(TVector<float, 3>(0, 0, 0));
		Particle->SetV(TVector<float, 3>(0, 0, 10));

		Solver->RegisterObject(Particle.Get());

		{
			TAtomic<int32> TestSequenceAtmoic(5);

			Chaos::FCommandList NewCommandList;
			NewCommandList.Enqueue([&TestSequenceAtmoic]() {
				TestSequenceAtmoic = TestSequenceAtmoic.Load() * 2;
			});

			NewCommandList.Enqueue([&TestSequenceAtmoic]() {
				TestSequenceAtmoic = TestSequenceAtmoic.Load() - 2;
			});

			NewCommandList.Enqueue([&TestSequenceAtmoic]() {
				TestSequenceAtmoic = TestSequenceAtmoic.Load() * 2;
			});

			EXPECT_EQ(TestSequenceAtmoic.Load(), 5);

			NewCommandList.Flush();

			FPlatformProcess::Sleep(2.0f);

			EXPECT_EQ(TestSequenceAtmoic.Load(), 16);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
	}

	template void CommandListTest<float>();
}
