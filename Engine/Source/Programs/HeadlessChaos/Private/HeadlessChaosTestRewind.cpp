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

	void TickSolverHelper(FChaosSolversModule* Module, FPhysicsSolver* Solver)
	{
		Solver->PushPhysicsState(Module->GetDispatcher());
		FPhysicsSolverAdvanceTask AdvanceTask(Solver,1.0f);
		AdvanceTask.DoTask(ENamedThreads::GameThread,FGraphEventRef());
		Solver->BufferPhysicsResults();
		Solver->FlipBuffers();
		Solver->UpdateGameThreadStructures();
	}

	GTEST_TEST(RewindTest,DataCapture)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(0),FVec3(1)));
		auto Box2 = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(2),FVec3(3)));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		FPhysicsSolver* Solver = Module->CreateSolver(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		// Make a particle

		auto Particle = TKinematicGeometryParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Solver->EnableRewindCapture(20);

		for(int Step = 0; Step < 11; ++Step)
		{
			//property that changes every step
			Particle->SetX(FVec3(0,0,100 - Step));

			//property that changes once half way through
			if(Step == 3)
			{
				Particle->SetGeometry(Box);
			}

			if(Step == 5)
			{
				Particle->SetGeometry(Box2);
			}

			if(Step == 7)
			{
				Particle->SetGeometry(Box);
			}

			TickSolverHelper(Module, Solver);
		}

		//ended up at z = 90
		EXPECT_EQ(Particle->X()[2],90);

		//ended up with box geometry
		EXPECT_EQ(Box.Get(),Particle->Geometry().Get());
		
		const FRewindData* RewindData = Solver->GetRewindData();

		//check state at every step except latest
		for(int Step = 0; Step < 10; ++Step)
		{
			const auto ParticleState = RewindData->GetStateAtFrame(*Particle,Step);

			EXPECT_TRUE(ParticleState != nullptr);
			if(ParticleState)
			{
				EXPECT_EQ(ParticleState->X()[2],100 - Step);

				if(Step < 3)
				{
					//was sphere
					EXPECT_EQ(ParticleState->Geometry().Get(),Sphere.Get());
				}
				else if(Step < 5 || Step >= 7)
				{
					//then became box
					EXPECT_EQ(ParticleState->Geometry().Get(),Box.Get());
				}
				else
				{
					//second box
					EXPECT_EQ(ParticleState->Geometry().Get(),Box2.Get());
				}
			}
		}
		
		//no data at head because we always save the previous frame
		EXPECT_TRUE(RewindData->GetStateAtFrame(*Particle,10) == nullptr);

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

}
