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

	template <typename TSolver>
	void TickSolverHelper(FChaosSolversModule* Module, TSolver* Solver, FReal Dt = 1.0)
	{
		Solver->PushPhysicsState(Module->GetDispatcher());
		FPhysicsSolverAdvanceTask AdvanceTask(Solver,Dt);
		AdvanceTask.DoTask(ENamedThreads::GameThread,FGraphEventRef());
		Solver->BufferPhysicsResults();
		Solver->FlipBuffers();
		Solver->UpdateGameThreadStructures();
	}

	TYPED_TEST(AllTraits, RewindTest_MovingGeomChange)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(0),FVec3(1)));
		auto Box2 = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(2),FVec3(3)));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(20);


		// Make particles
		auto Particle = TKinematicGeometryParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());

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
			const auto ParticleState = RewindData->GetPastStateAtFrame(*Particle,Step);
			EXPECT_EQ(ParticleState.X()[2],100 - Step);

			if(Step < 3)
			{
				//was sphere
				EXPECT_EQ(ParticleState.Geometry().Get(),Sphere.Get());
			}
			else if(Step < 5 || Step >= 7)
			{
				//then became box
				EXPECT_EQ(ParticleState.Geometry().Get(),Box.Get());
			}
			else
			{
				//second box
				EXPECT_EQ(ParticleState.Geometry().Get(),Box2.Get());
			}
		}
		
		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}


	TYPED_TEST(AllTraits, RewindTest_AddForce)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(20);


		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());

		for(int Step = 0; Step < 11; ++Step)
		{
			//sim-writable property that changes every step
			Particle->SetF(FVec3(0,0,Step + 1));


			TickSolverHelper(Module,Solver);
		}

		const FRewindData* RewindData = Solver->GetRewindData();

		//check state at every step except latest
		for(int Step = 0; Step < 10; ++Step)
		{
			const auto ParticleState = RewindData->GetPastStateAtFrame(*Particle,Step);
			EXPECT_EQ(ParticleState.F()[2],Step+1);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_IntermittentForce)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(20);


		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());

		for(int Step = 0; Step < 11; ++Step)
		{	
			//sim-writable property that changes infrequently and not at beginning
			if(Step == 3)
			{
				Particle->SetF(FVec3(0,0,Step));
			}

			if(Step == 5)
			{
				Particle->SetF(FVec3(0,0,Step));
			}

			TickSolverHelper(Module,Solver);
		}
		
		const FRewindData* RewindData = Solver->GetRewindData();

		//check state at every step except latest
		for(int Step = 0; Step < 10; ++Step)
		{
			const auto ParticleState = RewindData->GetPastStateAtFrame(*Particle,Step);

			if(Step == 3)
			{
				EXPECT_EQ(ParticleState.F()[2],3);
			}
			else if(Step == 5)
			{
				EXPECT_EQ(ParticleState.F()[2],5);
			}
			else
			{
				EXPECT_EQ(ParticleState.F()[2],0);
			}
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_IntermittentGeomChange)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(0),FVec3(1)));
		auto Box2 = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(2),FVec3(3)));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(20);


		// Make particles
		auto Particle = TKinematicGeometryParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());

		for(int Step = 0; Step < 11; ++Step)
		{
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

			TickSolverHelper(Module,Solver);
		}

		const FRewindData* RewindData = Solver->GetRewindData();

		//check state at every step except latest
		for(int Step = 0; Step < 10; ++Step)
		{
			const auto ParticleState = RewindData->GetPastStateAtFrame(*Particle,Step);

			
			if(Step < 3)
			{
				//was sphere
				EXPECT_EQ(ParticleState.Geometry().Get(),Sphere.Get());
			}
			else if(Step < 5 || Step >= 7)
			{
				//then became box
				EXPECT_EQ(ParticleState.Geometry().Get(),Box.Get());
			}
			else
			{
				//second box
				EXPECT_EQ(ParticleState.Geometry().Get(),Box2.Get());
			}
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_FallingObjectWithTeleport)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(20);


		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);
		Particle->SetX(FVec3(0,0,100));

		TArray<FVec3> X;
		TArray<FVec3> V;

		for(int Step = 0; Step < 10; ++Step)
		{
			//teleport from GT
			if(Step == 5)
			{
				Particle->SetX(FVec3(0,0,10));
				Particle->SetV(FVec3(0,0,1));
			}

			X.Add(Particle->X());
			V.Add(Particle->V());
			TickSolverHelper(Module,Solver);
		}

		const FRewindData* RewindData = Solver->GetRewindData();


		for(int Step = 0; Step < 9; ++Step)
		{
			const auto ParticleState = RewindData->GetPastStateAtFrame(*Particle,Step);
			
			EXPECT_EQ(ParticleState.X()[2],X[Step][2]);
			EXPECT_EQ(ParticleState.V()[2],V[Step][2]);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_ApplyRewind)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(20);


		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);
		Particle->SetX(FVec3(0,0,100));

		TArray<FVec3> X;
		TArray<FVec3> V;

		for(int Step = 0; Step < 10; ++Step)
		{
			//teleport from GT
			if(Step == 5)
			{
				Particle->SetX(FVec3(0,0,10));
				Particle->SetV(FVec3(0,0,1));
			}

			X.Add(Particle->X());
			V.Add(Particle->V());
			TickSolverHelper(Module,Solver);
		}
		X.Add(Particle->X());
		V.Add(Particle->V());

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(0));
		
		//make sure recorded data is still valid even at head
		for(int Step = 0; Step < 11; ++Step)
		{
			FGeometryParticleState State(*Particle);
			const EFutureQueryResult Status = RewindData->GetFutureStateAtFrame(State,Step);
			EXPECT_EQ(Status,EFutureQueryResult::Ok);
			EXPECT_EQ(State.X()[2],X[Step][2]);
			EXPECT_EQ(State.V()[2],V[Step][2]);
		}

		//rewind to each frame and make sure data is recorded
		for(int Step = 0; Step < 10; ++Step)
		{
			EXPECT_TRUE(RewindData->RewindToFrame(Step));
			EXPECT_EQ(Particle->X()[2],X[Step][2]);
			EXPECT_EQ(Particle->V()[2],V[Step][2]);
		}

		//can't rewind earlier than latest rewind
		EXPECT_FALSE(RewindData->RewindToFrame(5));

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_Remove)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(20);


		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);
		Particle->SetX(FVec3(0,0,100));

		TArray<FVec3> X;
		TArray<FVec3> V;

		for(int Step = 0; Step < 10; ++Step)
		{
			X.Add(Particle->X());
			V.Add(Particle->V());
			TickSolverHelper(Module,Solver);
		}

		FRewindData* RewindData = Solver->GetRewindData();

		{
			const FGeometryParticleState State = RewindData->GetPastStateAtFrame(*Particle,5);
			EXPECT_EQ(State.X(),X[5]);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		// State should be the same as being at head because we removed it from solver
		{
			const FGeometryParticleState State = RewindData->GetPastStateAtFrame(*Particle,5);
			EXPECT_EQ(Particle->X(), State.X());
		}

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_BufferLimit)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(5);


		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);
		Particle->SetX(FVec3(0,0,100));

		TArray<FVec3> X;
		TArray<FVec3> V;

		const int32 NumSteps = 20;
		for(int Step = 0; Step < NumSteps; ++Step)
		{
			//teleport from GT
			if(Step == 15)
			{
				Particle->SetX(FVec3(0,0,10));
				Particle->SetV(FVec3(0,0,1));
			}

			X.Add(Particle->X());
			V.Add(Particle->V());
			TickSolverHelper(Module,Solver);
		}
		X.Add(Particle->X());
		V.Add(Particle->V());

		FRewindData* RewindData = Solver->GetRewindData();
		const int32 LastValidStep = NumSteps - 1;
		const int32 FirstValid = NumSteps - RewindData->Capacity() + 1;	//we lose 1 step because we have to save head
		for(int Step = 0; Step < FirstValid; ++Step)
		{
			//can't go back that far
			EXPECT_FALSE(RewindData->RewindToFrame(Step));
		}

		for(int Step = FirstValid; Step <= LastValidStep; ++Step)
		{
			EXPECT_TRUE(RewindData->RewindToFrame(Step));
			EXPECT_EQ(Particle->X()[2],X[Step][2]);
			EXPECT_EQ(Particle->V()[2],V[Step][2]);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_NumDirty)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(5);


		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);
		
		for(int Step = 0; Step < 10; ++Step)
		{
			TickSolverHelper(Module,Solver);

			const FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_EQ(RewindData->GetNumDirtyParticles(),1);
		}

		//stop movement
		Particle->SetGravityEnabled(false);
		Particle->SetV(FVec3(0));

		for(int Step = 0; Step < 10; ++Step)
		{
			TickSolverHelper(Module,Solver);
		}

		{
			//enough frames with no changes so no longer dirty
			const FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_EQ(RewindData->GetNumDirtyParticles(),0);
		}

		{
			//single change so back to being dirty
			Particle->SetGravityEnabled(true);
			TickSolverHelper(Module,Solver);

			const FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_EQ(RewindData->GetNumDirtyParticles(),1);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_Resim)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(5);

		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);

		auto Kinematic = TKinematicGeometryParticle<float,3>::CreateParticle();

		Kinematic->SetGeometry(Sphere);
		Solver->RegisterObject(Kinematic.Get());
		Kinematic->SetX(FVec3(2,2,2));

		TArray<FVec3> X;
		const int32 LastStep = 12;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			X.Add(Particle->X());

			if(Step == 8)
			{
				Kinematic->SetX(FVec3(50,50,50));
			}

			if(Step == 10)
			{
				Kinematic->SetX(FVec3(60,60,60));
			}

			TickSolverHelper(Module,Solver);
		}

		const int RewindStep = 7;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		//Move particle and rerun
		Particle->SetX(FVec3(0,0,100));
		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			if(Step == 8)
			{
				Kinematic->SetX(FVec3(50));
			}

			X[Step] = Particle->X();
			TickSolverHelper(Module,Solver);

			//see that particle has desynced
			if(Step < LastStep)
			{
				//If we're still in the past make sure future has been marked as desync
				FGeometryParticleState State(*Particle);
				EXPECT_EQ(EFutureQueryResult::Desync,RewindData->GetFutureStateAtFrame(State,Step));

				FGeometryParticleState KinState(*Kinematic);
				const EFutureQueryResult KinFutureStatus = RewindData->GetFutureStateAtFrame(KinState,Step);
				if(Step < 10)
				{
					EXPECT_EQ(KinFutureStatus,EFutureQueryResult::Ok);
				}
				else
				{
					EXPECT_EQ(KinFutureStatus,EFutureQueryResult::Desync);
				}
			}
		}

		EXPECT_EQ(Kinematic->X()[2],50);	//Rewound kinematic and only did one update, so use that first update

		//Make sure we recorded the new data
		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			const FGeometryParticleState State = RewindData->GetPastStateAtFrame(*Particle,Step);
			EXPECT_EQ(State.X()[2],X[Step][2]);

			const FGeometryParticleState KinState = RewindData->GetPastStateAtFrame(*Kinematic,Step);
			if(Step < 8)
			{
				EXPECT_EQ(KinState.X()[2],2);
			}
			else
			{
				EXPECT_EQ(KinState.X()[2],50);	//in resim we didn't do second move, so recorded data must be updated
			}
		}



		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_ResimDesyncAfterMissingTeleport)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(7);

		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);

		const int LastStep = 11;
		TArray<FVec3> X;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			if(Step == 7)
			{
				Particle->SetX(FVec3(0,0,5));
			}

			if(Step == 9)
			{
				Particle->SetX(FVec3(0,0,1));
			}
			X.Add(Particle->X());
			TickSolverHelper(Module,Solver);
		}
		X.Add(Particle->X());

		const int RewindStep = 5;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			FGeometryParticleState FutureState(*Particle);
			EXPECT_EQ(RewindData->GetFutureStateAtFrame(FutureState,Step+1), Step < 10 ? EFutureQueryResult::Ok : EFutureQueryResult::Desync);
			if(Step < 10)
			{
				EXPECT_EQ(X[Step+1][2],FutureState.X()[2]);
			}

			if(Step == 7)
			{
				Particle->SetX(FVec3(0,0,5));
			}

			//skip step 9 SetX to trigger a desync

			TickSolverHelper(Module,Solver);

			//can't compare future with end of frame because we overwrite the result
			if(Step != 6 && Step != 8 && Step < 9)
			{
				EXPECT_EQ(Particle->X()[2],FutureState.X()[2]);
			}
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_ResimDesyncAfterChangingMass)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(7);

		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);

		FReal CurMass = 1.0;
		Particle->SetM(CurMass);
		int32 LastStep = 11;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			if(Step == 7)
			{
				Particle->SetM(2);
			}

			if(Step == 9)
			{
				Particle->SetM(3);
			}
			TickSolverHelper(Module,Solver);
		}

		const int RewindStep = 5;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			FGeometryParticleState FutureState(*Particle);
			EXPECT_EQ(RewindData->GetFutureStateAtFrame(FutureState,Step),Step < 10 ? EFutureQueryResult::Ok : EFutureQueryResult::Desync);
			if(Step < 7)
			{
				EXPECT_EQ(1,FutureState.M());
			}

			if(Step == 7)
			{
				Particle->SetM(2);
			}

			//skip step 9 SetM to trigger a desync

			TickSolverHelper(Module,Solver);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, DISABLED_RewindTest_DesyncFromPT)
	{
		//We want to detect when sim results change
		//Detecting output of position and velocity is expensive and hard to track
		//Instead we need to rely on fast forward mechanism, this is still in progress
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100, 100, 0)));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);
		Solver->EnableRewindCapture(7);

		// Make particles
		auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();
		auto Kinematic = TKinematicGeometryParticle<float,3>::CreateParticle();

		Dynamic->SetGeometry(Sphere);
		Dynamic->SetGravityEnabled(true);
		Solver->RegisterObject(Dynamic.Get());

		Kinematic->SetGeometry(Box);
		Solver->RegisterObject(Kinematic.Get());

		Dynamic->SetX(FVec3(0,0,17));
		Dynamic->SetGravityEnabled(false);
		Dynamic->SetV(FVec3(0,0,-1));
		Dynamic->SetObjectState(EObjectStateType::Dynamic);

		Kinematic->SetX(FVec3(0,0,0));

		ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

		const int32 LastStep = 11;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			TickSolverHelper(Module,Solver);
		}

		EXPECT_FLOAT_EQ(Dynamic->X()[2],10);
		
		const int RewindStep = 5;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		Kinematic->SetX(FVec3(0,0,-1));

		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			//at Step 7 we're at z=10 but velocity will now be -1 instead of 0, so a desync has occured
			FGeometryParticleState FutureState(*Dynamic);
			EXPECT_EQ(RewindData->GetFutureStateAtFrame(FutureState,Step),Step < 7 ? EFutureQueryResult::Ok : EFutureQueryResult::Desync);

			TickSolverHelper(Module,Solver);
		}

		EXPECT_FLOAT_EQ(Dynamic->X()[2],9);

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_DeltaTimeRecord)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(7);

		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(true);

		const int LastStep = 11;
		TArray<FReal> DTs;
		FReal Dt = 1;
		for(int Step = 0; Step <= LastStep; ++Step)
		{
			DTs.Add(Dt);
			TickSolverHelper(Module,Solver, Dt);
			Dt += 0.1;
		}
		
		const int RewindStep = 5;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			EXPECT_EQ(DTs[Step],RewindData->GetDeltaTimeForFrame(Step));
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_ResimDesyncFromChangeForce)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);

		Solver->EnableRewindCapture(7);

		// Make particles
		auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

		Particle->SetGeometry(Sphere);
		Solver->RegisterObject(Particle.Get());
		Particle->SetGravityEnabled(false);
		Particle->SetV(FVec3(0,0,10));

		int32 LastStep = 11;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			if(Step == 7)
			{
				Particle->SetF(FVec3(0,1,0));
			}

			if(Step == 9)
			{
				Particle->SetF(FVec3(100,0,0));
			}
			TickSolverHelper(Module,Solver);
		}

		const int RewindStep = 5;

		{
			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				FGeometryParticleState FutureState(*Particle);
				EXPECT_EQ(RewindData->GetFutureStateAtFrame(FutureState,Step),Step < 10 ? EFutureQueryResult::Ok : EFutureQueryResult::Desync);

				if(Step == 7)
				{
					Particle->SetF(FVec3(0,1,0));
				}

				//skip step 9 SetF to trigger a desync

				TickSolverHelper(Module,Solver);
			}
			EXPECT_EQ(Particle->V()[0],0);
		}

		//rewind to exactly step 7 to make sure force is not already applied for us
		{
			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(7));
			EXPECT_EQ(Particle->F()[1],0);
		}

		// Throw out the proxy
		Solver->UnregisterObject(Particle.Get());

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits,RewindTest_ResimAsSlave)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100,100,0)));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();
		Module->ChangeThreadingMode(EChaosThreadingMode::SingleThread);

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr,ESolverFlags::Standalone);
		Solver->SetEnabled(true);
		Solver->EnableRewindCapture(7);

		// Make particles
		auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();
		auto Kinematic = TKinematicGeometryParticle<float,3>::CreateParticle();

		Dynamic->SetGeometry(Sphere);
		Dynamic->SetGravityEnabled(true);
		Solver->RegisterObject(Dynamic.Get());

		Kinematic->SetGeometry(Box);
		Solver->RegisterObject(Kinematic.Get());

		Dynamic->SetX(FVec3(0,0,17));
		Dynamic->SetGravityEnabled(false);
		Dynamic->SetV(FVec3(0,0,-1));
		Dynamic->SetObjectState(EObjectStateType::Dynamic);
		Dynamic->SetResimType(EResimType::SimAsSlave);

		Kinematic->SetX(FVec3(0,0,0));

		ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

		const int32 LastStep = 11;

		TArray<FVec3> Xs;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			TickSolverHelper(Module,Solver);
			Xs.Add(Dynamic->X());
		}

		EXPECT_FLOAT_EQ(Dynamic->X()[2],10);

		const int RewindStep = 5;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		//make avoid collision
		Kinematic->SetX(FVec3(0,0,100000));

		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			//Resim but dynamic will take old path since it's marked as ResimAsSlave
			TickSolverHelper(Module,Solver);

			EXPECT_VECTOR_FLOAT_EQ(Dynamic->X(),Xs[Step]);
		}

		EXPECT_FLOAT_EQ(Dynamic->X()[2],10);

		Module->DestroySolver(Solver);
	}
}
