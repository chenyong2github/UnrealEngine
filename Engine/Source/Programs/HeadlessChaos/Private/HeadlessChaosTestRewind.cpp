// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaosTestUtility.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ErrorReporter.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Chaos/Utilities.h"
#include "PBDRigidsSolver.h"
#include "ChaosSolversModule.h"

#include "Modules/ModuleManager.h"
#include "Framework/PhysicsTickTask.h"
#include "RewindData.h"


namespace ChaosTest {

    using namespace Chaos;

	GTEST_TEST(RewindTest,DataCapture)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		Chaos::FPhysicsSolver* Solver = Module->CreateSolver(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		// Make a particle

		auto Particle = Chaos::TKinematicGeometryParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Solver->EnableRewindCapture(20);

		for(int Step = 0; Step < 11; ++Step)
		{
			Particle->SetX(FVec3(0,0,100 - Step));

			Solver->PushPhysicsState(Module->GetDispatcher());
			FPhysicsSolverAdvanceTask AdvanceTask(Solver,1.0f);
			AdvanceTask.DoTask(ENamedThreads::GameThread,FGraphEventRef());
			Solver->BufferPhysicsResults();
			Solver->FlipBuffers();
			Solver->UpdateGameThreadStructures();
		}

		//ended up at z = 90
		EXPECT_EQ(Particle->X()[2],90);

		const FRewindData* RewindData = Solver->GetRewindData();

		//check state at every step except latest
		for(int Step = 0; Step < 10; ++Step)
		{
			const auto ParticleState = RewindData->GetStateAtFrame(*Particle,Step);

			EXPECT_TRUE(ParticleState != nullptr);
			if(ParticleState)
			{
				EXPECT_EQ(ParticleState->X()[2],100 - Step);
			}
		}
		
		//no data at head because we always save the previous frame
		EXPECT_TRUE(RewindData->GetStateAtFrame(*Particle,10) == nullptr);
		
		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

}
