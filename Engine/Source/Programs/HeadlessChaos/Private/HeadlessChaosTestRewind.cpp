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
#include "RewindData.h"
#include "GeometryCollection/GeometryCollectionTestFramework.h"


namespace ChaosTest {

    using namespace Chaos;
	using namespace GeometryCollectionTest;

	template <typename TSolver>
	void TickSolverHelper(FChaosSolversModule* Module, TSolver* Solver, FReal Dt = 1.0)
	{
		Solver->AdvanceAndDispatch_External(Dt);
		Solver->BufferPhysicsResults();
		Solver->FlipBuffers();
		Solver->UpdateGameThreadStructures();
	}

	TYPED_TEST(AllTraits, RewindTest_MovingGeomChange)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(0),FVec3(1)));
			auto Box2 = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(2),FVec3(3)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


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
	}


	TYPED_TEST(AllTraits, RewindTest_AddForce)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


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
	}

	TYPED_TEST(AllTraits, RewindTest_IntermittentForce)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


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
	}

	TYPED_TEST(AllTraits, RewindTest_IntermittentGeomChange)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(0),FVec3(1)));
			auto Box2 = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<float,3>(FVec3(2),FVec3(3)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


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
	}

	TYPED_TEST(AllTraits, RewindTest_FallingObjectWithTeleport)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


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
	}

	TYPED_TEST(AllTraits,RewindTest_ResimFallingObjectWithTeleport)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


			// Make particles
			auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

			Particle->SetGeometry(Sphere);
			Solver->RegisterObject(Particle.Get());
			Particle->SetGravityEnabled(true);
			Particle->SetX(FVec3(0,0,100));

			TArray<FVec3> XPre;
			TArray<FVec3> VPre;
			TArray<FVec3> XPost;
			TArray<FVec3> VPost;

			for(int Step = 0; Step < 10; ++Step)
			{
				//teleport from GT
				if(Step == 5)
				{
					Particle->SetX(FVec3(0,0,10));
					Particle->SetV(FVec3(0,0,1));
				}

				XPre.Add(Particle->X());
				VPre.Add(Particle->V());

				TickSolverHelper(Module,Solver);

				XPost.Add(Particle->X());
				VPost.Add(Particle->V());
			}

			FRewindData* RewindData = Solver->GetRewindData();
			RewindData->RewindToFrame(0);

			for(int Step = 0; Step < 10; ++Step)
			{
				//teleport from GT
				if(Step == 5)
				{
					Particle->SetX(FVec3(0,0,10));
					Particle->SetV(FVec3(0,0,1));
				}

				EXPECT_EQ(Particle->X()[2],XPre[Step][2]);
				EXPECT_EQ(Particle->V()[2],VPre[Step][2]);
				TickSolverHelper(Module,Solver);
				EXPECT_EQ(Particle->X()[2],XPost[Step][2]);
				EXPECT_EQ(Particle->V()[2],VPost[Step][2]);
			}

			//no desync so should be empty
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),0);

			// Throw out the proxy
			Solver->UnregisterObject(Particle.Get());

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_ResimFallingObjectWithTeleportAsSlave)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


			// Make particles
			auto Particle = TPBDRigidParticle<float,3>::CreateParticle();

			Particle->SetGeometry(Sphere);
			Solver->RegisterObject(Particle.Get());
			Particle->SetGravityEnabled(true);
			Particle->SetX(FVec3(0,0,100));
			Particle->SetResimType(EResimType::ResimAsSlave);

			TArray<FVec3> XPre;
			TArray<FVec3> VPre;
			TArray<FVec3> XPost;
			TArray<FVec3> VPost;

			for(int Step = 0; Step < 10; ++Step)
			{
				//teleport from GT
				if(Step == 5)
				{
					Particle->SetX(FVec3(0,0,10));
					Particle->SetV(FVec3(0,0,1));
				}

				XPre.Add(Particle->X());
				VPre.Add(Particle->V());

				TickSolverHelper(Module,Solver);

				XPost.Add(Particle->X());
				VPost.Add(Particle->V());
			}

			FRewindData* RewindData = Solver->GetRewindData();
			RewindData->RewindToFrame(0);

			for(int Step = 0; Step < 10; ++Step)
			{
				//teleport done automatically, but inside the solve
				if(Step != 5)
				{
					EXPECT_EQ(Particle->X()[2],XPre[Step][2]);
					EXPECT_EQ(Particle->V()[2],VPre[Step][2]);
				}

				TickSolverHelper(Module,Solver);
			
				//Make sure sets particle to end of sim at this frame, not beginning of next frame
				EXPECT_EQ(Particle->X()[2],XPost[Step][2]);
				EXPECT_EQ(Particle->V()[2],VPost[Step][2]);
			}

			//no desync so should be empty
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),0);

			// Throw out the proxy
			Solver->UnregisterObject(Particle.Get());

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits, RewindTest_ApplyRewind)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


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

			//no desync so should be empty
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),0);

			//can't rewind earlier than latest rewind
			EXPECT_FALSE(RewindData->RewindToFrame(5));

			// Throw out the proxy
			Solver->UnregisterObject(Particle.Get());

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits, RewindTest_Remove)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(20, !!Optimization);


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
			
			//Unregister enqueues commands which won't run until next tick.
			//Use this callback to inspect state after commands, but before sim of next step
			Solver->RegisterSimOneShotCallback([&]()
			{
				// State should be the same as being at head because we removed it from solver
				{
					const FGeometryParticleState State = RewindData->GetPastStateAtFrame(*Particle,5);
					EXPECT_EQ(Particle->X(),State.X());
				}
			});

			TickSolverHelper(Module,Solver);

			

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits, RewindTest_BufferLimit)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(5 , !!Optimization);


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
	}

	TYPED_TEST(AllTraits, RewindTest_NumDirty)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			//note: this 5 is just a suggestion, there could be more frames saved than that
			Solver->EnableRewindCapture(5 , !!Optimization);


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

			// Wait for sleep (active particles get added to the dirty list)
			// NOTE: Sleep requires 20 frames of inactivity by default, plus the time for smoothed velocity to damp to zero
			// (see FPBDConstraintGraph::SleepInactive)
			for(int Step = 0; Step < 500; ++Step)
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
	}

	TYPED_TEST(AllTraits, RewindTest_Resim)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(5 , !!Optimization);

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
			Kinematic->SetX(FVec3(2));
			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				if(Step == 8)
				{
					Kinematic->SetX(FVec3(50));
				}

				X[Step] = Particle->X();
				TickSolverHelper(Module,Solver);

				auto PTParticle = static_cast<FSingleParticlePhysicsProxy<TPBDRigidParticle<FReal,3>>*>(Particle->GetProxy())->GetHandle();
				auto PTKinematic = static_cast<FSingleParticlePhysicsProxy<TKinematicGeometryParticle<FReal,3>>*>(Kinematic->GetProxy())->GetHandle();

				//see that particle has desynced
				if(Step < LastStep)
				{
					//If we're still in the past make sure future has been marked as desync
					FGeometryParticleState State(*Particle);
					EXPECT_EQ(EFutureQueryResult::Desync,RewindData->GetFutureStateAtFrame(State,Step));
					EXPECT_EQ(PTParticle->SyncState(),ESyncState::HardDesync);

					FGeometryParticleState KinState(*Kinematic);
					const EFutureQueryResult KinFutureStatus = RewindData->GetFutureStateAtFrame(KinState,Step);
					if(Step < 10)
					{
						EXPECT_EQ(KinFutureStatus,EFutureQueryResult::Ok);
						EXPECT_EQ(PTKinematic->SyncState(),ESyncState::InSync);
					}
					else
					{
						EXPECT_EQ(KinFutureStatus,EFutureQueryResult::Desync);
						EXPECT_EQ(PTKinematic->SyncState(),ESyncState::HardDesync);
					}
				}
				else
				{
					//Last resim frame ran so everything is marked as in sync
					EXPECT_EQ(PTParticle->SyncState(),ESyncState::InSync);
					EXPECT_EQ(PTKinematic->SyncState(),ESyncState::InSync);
				}
			}

			//expect both particles to be hard desynced
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),2);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[1].MostDesynced,ESyncState::HardDesync);

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
	}

	TYPED_TEST(AllTraits, RewindTest_ResimDesyncAfterMissingTeleport)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

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

			//expected desync
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),1);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[0].Particle,Particle.Get());

			// Throw out the proxy
			Solver->UnregisterObject(Particle.Get());

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits, RewindTest_ResimDesyncAfterChangingMass)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

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

			//expected desync
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),1);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[0].Particle,Particle.Get());

			// Throw out the proxy
			Solver->UnregisterObject(Particle.Get());

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits, RewindTest_DesyncFromPT)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			//We want to detect when sim results change
			//Detecting output of position and velocity is expensive and hard to track
			//Instead we need to rely on fast forward mechanism, this is still in progress
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100, 100, 0)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

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

			// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
			EXPECT_GE(Dynamic->X()[2], 10);
			EXPECT_LE(Dynamic->X()[2], 11);
		
			const int RewindStep = 5;

			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			Kinematic->SetX(FVec3(0,0,-1));

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				//at the end of frame 6 a desync occurs because velocity is no longer clamped (kinematic moved)
				//because of this desync will happen for any step after 6
				if(Step <= 6)
				{
					FGeometryParticleState FutureState(*Dynamic);
					EXPECT_EQ(RewindData->GetFutureStateAtFrame(FutureState,Step),EFutureQueryResult::Ok);
				}
				else if(Step >= 8)
				{
					//collision would have happened at frame 7, so anything after will desync. We skip a few frames because solver is fuzzy at that point
					//that is we can choose to solve velocity in a few ways. Main thing we want to know is that a desync eventually happened
					FGeometryParticleState FutureState(*Dynamic);
					EXPECT_EQ(RewindData->GetFutureStateAtFrame(FutureState,Step),EFutureQueryResult::Desync);
				}
				

				TickSolverHelper(Module,Solver);
			}

			//both kinematic and simulated are desynced
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),2);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[1].MostDesynced,ESyncState::HardDesync);

			// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
			EXPECT_GE(Dynamic->X()[2],9);
			EXPECT_LE(Dynamic->X()[2], 10);

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits, RewindTest_DeltaTimeRecord)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);
			
			Solver->EnableRewindCapture(7, !!Optimization);

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
	}

	TYPED_TEST(AllTraits, RewindTest_ResimDesyncFromChangeForce)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<float,3>(TVector<float,3>(0),10));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

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

				//desync
				const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
				EXPECT_EQ(DesyncedParticles.Num(),1);
				EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
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
	}

	TYPED_TEST(AllTraits,RewindTest_ResimAsSlave)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100,100,0)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

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
			Dynamic->SetResimType(EResimType::ResimAsSlave);

			Kinematic->SetX(FVec3(0,0,0));

			ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

			const int32 LastStep = 11;

			TArray<FVec3> Xs;

			for(int Step = 0; Step <= LastStep; ++Step)
			{
				TickSolverHelper(Module,Solver);
				Xs.Add(Dynamic->X());
			}


			EXPECT_GE(Dynamic->X()[2], 10);
			EXPECT_LE(Dynamic->X()[2], 11);

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

			//slave so dynamic in sync, kinematic desync
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),1);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[0].Particle,Kinematic.Get());

			// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
			EXPECT_GE(Dynamic->X()[2],10);
			EXPECT_LE(Dynamic->X()[2], 11);

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_FullResimFallSeeCollisionCorrection)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100,100,0)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(100, !!Optimization);

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

			Kinematic->SetX(FVec3(0,0,-1000));

			ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

			const int32 LastStep = 11;

			TArray<FVec3> Xs;

			for(int Step = 0; Step <= LastStep; ++Step)
			{
				TickSolverHelper(Module,Solver);
				Xs.Add(Dynamic->X());
			}

			// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
			EXPECT_GE(Dynamic->X()[2], 5);
			EXPECT_LE(Dynamic->X()[2], 6);

			const int RewindStep = 0;

			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			//force collision
			Kinematic->SetX(FVec3(0,0,0));

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				//Resim sees collision since it's ResimAsFull
				TickSolverHelper(Module,Solver);
				EXPECT_GE(Dynamic->X()[2],10);
			}

			// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
			EXPECT_GE(Dynamic->X()[2], 10);
			EXPECT_LE(Dynamic->X()[2], 11);

			//both desync
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),2);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[1].MostDesynced,ESyncState::HardDesync);

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_ResimAsSlaveFallIgnoreCollision)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100,100,0)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(100, !!Optimization);

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
			Dynamic->SetResimType(EResimType::ResimAsSlave);

			Kinematic->SetX(FVec3(0,0,-1000));

			ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

			const int32 LastStep = 11;

			TArray<FVec3> Xs;

			for(int Step = 0; Step <= LastStep; ++Step)
			{
				TickSolverHelper(Module,Solver);
				Xs.Add(Dynamic->X());
			}

			// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
			EXPECT_GE(Dynamic->X()[2], 5);
			EXPECT_LE(Dynamic->X()[2], 6);

			const int RewindStep = 0;

			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			//force collision
			Kinematic->SetX(FVec3(0,0,0));

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				//Resim ignores collision since it's ResimAsSlave
				TickSolverHelper(Module,Solver);

				EXPECT_VECTOR_FLOAT_EQ(Dynamic->X(),Xs[Step]);
			}

			// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
			EXPECT_GE(Dynamic->X()[2], 5);
			EXPECT_LE(Dynamic->X()[2], 6);

			//dynamic slave so only kinematic desyncs
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),1);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[0].Particle,Kinematic.Get());

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_ResimAsSlaveWithForces)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-10,-10,-10),FVec3(10,10,10)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

			// Make particles
			auto FullSim = TPBDRigidParticle<float,3>::CreateParticle();
			auto SlaveSim = TPBDRigidParticle<float,3>::CreateParticle();

			FullSim->SetGeometry(Box);
			FullSim->SetGravityEnabled(false);
			Solver->RegisterObject(FullSim.Get());

			SlaveSim->SetGeometry(Box);
			SlaveSim->SetGravityEnabled(false);
			Solver->RegisterObject(SlaveSim.Get());

			FullSim->SetX(FVec3(0,0,20));
			FullSim->SetObjectState(EObjectStateType::Dynamic);
			FullSim->SetM(1);
			FullSim->SetInvM(1);

			SlaveSim->SetX(FVec3(0,0,0));
			SlaveSim->SetResimType(EResimType::ResimAsSlave);
			SlaveSim->SetM(1);
			SlaveSim->SetInvM(1);

			ChaosTest::SetParticleSimDataToCollide({FullSim.Get(),SlaveSim.Get()});

			const int32 LastStep = 11;

			TArray<FVec3> Xs;

			for(int Step = 0; Step <= LastStep; ++Step)
			{
				SlaveSim->SetLinearImpulse(FVec3(0,0,0.5));
				TickSolverHelper(Module,Solver);
				Xs.Add(FullSim->X());
			}

			const int RewindStep = 5;

			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				//resim - slave sim should have its impulses automatically added thus moving FullSim in the exact same way
				TickSolverHelper(Module,Solver);

				EXPECT_VECTOR_FLOAT_EQ(FullSim->X(),Xs[Step]);
			}

			//slave so no desync
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),0);

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_ResimAsSlaveWokenUp)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-10,-10,-10),FVec3(10,10,10)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

			// Make particles
			auto ImpulsedObj = TPBDRigidParticle<float,3>::CreateParticle();
			auto HitObj = TPBDRigidParticle<float,3>::CreateParticle();

			ImpulsedObj->SetGeometry(Box);
			ImpulsedObj->SetGravityEnabled(false);
			Solver->RegisterObject(ImpulsedObj.Get());

			HitObj->SetGeometry(Box);
			HitObj->SetGravityEnabled(false);
			Solver->RegisterObject(HitObj.Get());

			ImpulsedObj->SetX(FVec3(0,0,20));
			ImpulsedObj->SetM(1);
			ImpulsedObj->SetInvM(1);
			ImpulsedObj->SetResimType(EResimType::ResimAsSlave);
			ImpulsedObj->SetObjectState(EObjectStateType::Sleeping);

			HitObj->SetX(FVec3(0,0,0));
			HitObj->SetM(1);
			HitObj->SetInvM(1);
			HitObj->SetResimType(EResimType::ResimAsSlave);
			HitObj->SetObjectState(EObjectStateType::Sleeping);


			ChaosTest::SetParticleSimDataToCollide({ImpulsedObj.Get(),HitObj.Get()});

			const int32 ApplyImpulseStep = 8;
			const int32 LastStep = 11;

			TArray<FVec3> Xs;

			for(int Step = 0; Step <= LastStep; ++Step)
			{
				if(ApplyImpulseStep == Step)
				{
					ImpulsedObj->SetLinearImpulse(FVec3(0,0,-10));
				}

				TickSolverHelper(Module,Solver);
				Xs.Add(HitObj->X());
			}

			const int RewindStep = 5;

			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				TickSolverHelper(Module,Solver);

				EXPECT_VECTOR_FLOAT_EQ(HitObj->X(),Xs[Step]);
			}

			//slave so no desync
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),0);

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_ResimAsSlaveWokenUpNoHistory)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-10,-10,-10),FVec3(10,10,10)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(7, !!Optimization);

			// Make particles
			auto ImpulsedObj = TPBDRigidParticle<float,3>::CreateParticle();
			auto HitObj = TPBDRigidParticle<float,3>::CreateParticle();

			ImpulsedObj->SetGeometry(Box);
			ImpulsedObj->SetGravityEnabled(false);
			Solver->RegisterObject(ImpulsedObj.Get());

			HitObj->SetGeometry(Box);
			HitObj->SetGravityEnabled(false);
			Solver->RegisterObject(HitObj.Get());

			ImpulsedObj->SetX(FVec3(0,0,20));
			ImpulsedObj->SetM(1);
			ImpulsedObj->SetInvM(1);
			ImpulsedObj->SetObjectState(EObjectStateType::Sleeping);

			HitObj->SetX(FVec3(0,0,0));
			HitObj->SetM(1);
			HitObj->SetInvM(1);
			HitObj->SetResimType(EResimType::ResimAsSlave);
			HitObj->SetObjectState(EObjectStateType::Sleeping);


			ChaosTest::SetParticleSimDataToCollide({ImpulsedObj.Get(),HitObj.Get()});

			const int32 ApplyImpulseStep = 97;
			const int32 LastStep = 100;

			TArray<FVec3> Xs;

			for(int Step = 0; Step <= LastStep; ++Step)
			{
				TickSolverHelper(Module,Solver);
				Xs.Add(HitObj->X());	//not a full re-sim so we should end up with exact same result
			}

			const int RewindStep = 95;

			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				//during resim apply correction impulse
				if(ApplyImpulseStep == Step)
				{
					ImpulsedObj->SetLinearImpulse(FVec3(0,0,-10));
				}

				TickSolverHelper(Module,Solver);

				//even though there's now a different collision in the sim, the final result of slave is the same as before
				EXPECT_VECTOR_FLOAT_EQ(HitObj->X(),Xs[Step]);
			}

			//only desync non-slave
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),1);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[0].Particle, ImpulsedObj.Get());

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_DesyncSimOutOfCollision)
	{
		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			if(TypeParam::IsRewindable() == false){ return; }
			auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
			auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100,100,0)));

			FChaosSolversModule* Module = FChaosSolversModule::GetModule();

			// Make a solver
			auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
			InitSolverSettings(Solver);

			Solver->EnableRewindCapture(100, !!Optimization);

			// Make particles
			auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();
			auto Kinematic = TKinematicGeometryParticle<float,3>::CreateParticle();

			Dynamic->SetGeometry(Sphere);
			Dynamic->SetGravityEnabled(true);
			Solver->RegisterObject(Dynamic.Get());
			Solver->GetEvolution()->GetGravityForces().SetAcceleration(FVec3(0, 0, -1));

			Kinematic->SetGeometry(Box);
			Solver->RegisterObject(Kinematic.Get());

			Dynamic->SetX(FVec3(0,0,17));
			Dynamic->SetGravityEnabled(true);
			Dynamic->SetObjectState(EObjectStateType::Dynamic);

			Kinematic->SetX(FVec3(0,0,0));

			ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

			const int32 LastStep = 11;

			TArray<FVec3> Xs;

			for(int Step = 0; Step <= LastStep; ++Step)
			{
				TickSolverHelper(Module,Solver);
				Xs.Add(Dynamic->X());
			}

			EXPECT_GE(Dynamic->X()[2],10);

			const int RewindStep = 8;

			FRewindData* RewindData = Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			//remove from collision, should wakeup entire island and force a soft desync
			Kinematic->SetX(FVec3(0,0,-10000));

			auto PTDynamic = static_cast<FSingleParticlePhysicsProxy<TPBDRigidParticle<FReal,3>>*>(Dynamic->GetProxy())->GetHandle();
			auto PTKinematic = static_cast<FSingleParticlePhysicsProxy<TKinematicGeometryParticle<FReal,3>>*>(Kinematic->GetProxy())->GetHandle();

			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				//physics sim desync will not be known until the next frame because we can only compare inputs (teleport overwrites result of end of frame for example)
				if(Step > RewindStep+1)
				{
					EXPECT_EQ(PTDynamic->SyncState(),ESyncState::HardDesync);
				}

				TickSolverHelper(Module,Solver);
				EXPECT_LE(Dynamic->X()[2],10);

				//kinematic desync will be known at end of frame because the simulation doesn't write results (so we know right away it's a desync)
				if(Step < LastStep)
				{
					EXPECT_EQ(PTKinematic->SyncState(),ESyncState::HardDesync);
				}
				else
				{
					//everything in sync after last step
					EXPECT_EQ(PTKinematic->SyncState(),ESyncState::InSync);
					EXPECT_EQ(PTDynamic->SyncState(),ESyncState::InSync);
				}
			
			}

			//both desync
			const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
			EXPECT_EQ(DesyncedParticles.Num(),2);
			EXPECT_EQ(DesyncedParticles[0].MostDesynced,ESyncState::HardDesync);
			EXPECT_EQ(DesyncedParticles[1].MostDesynced,ESyncState::HardDesync);

			Module->DestroySolver(Solver);
		}
	}

	TYPED_TEST(AllTraits,RewindTest_SoftDesyncFromSameIsland)
	{
		if(TypeParam::IsRewindable() == false){ return; }
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-100),FVec3(100,100,0)));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
		InitSolverSettings(Solver);

		Solver->EnableRewindCapture(100,true);	//soft desync only exists when resim optimization is on

		// Make particles
		auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();
		auto Kinematic = TKinematicGeometryParticle<float,3>::CreateParticle();

		Dynamic->SetGeometry(Sphere);
		Dynamic->SetGravityEnabled(true);
		Solver->RegisterObject(Dynamic.Get());
		Solver->GetEvolution()->GetGravityForces().SetAcceleration(FVec3(0,0,-1));

		Kinematic->SetGeometry(Box);
		Solver->RegisterObject(Kinematic.Get());

		Dynamic->SetX(FVec3(0,0,37));
		Dynamic->SetGravityEnabled(true);
		Dynamic->SetObjectState(EObjectStateType::Dynamic);

		Kinematic->SetX(FVec3(0,0,0));

		ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

		const int32 LastStep = 11;

		TArray<FVec3> Xs;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			TickSolverHelper(Module,Solver);
			Xs.Add(Dynamic->X());
		}

		// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
		EXPECT_GE(Dynamic->X()[2], 10);
		EXPECT_LE(Dynamic->X()[2], 12);

		const int RewindStep = 0;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		//mark kinematic as desynced (this should give us identical results which will trigger all particles in island to be soft desync)
		
		auto PTDynamic = static_cast<FSingleParticlePhysicsProxy<TPBDRigidParticle<FReal,3>>*>(Dynamic->GetProxy())->GetHandle();
		auto PTKinematic = static_cast<FSingleParticlePhysicsProxy<TKinematicGeometryParticle<FReal,3>>*>(Kinematic->GetProxy())->GetHandle();
		PTKinematic->SetSyncState(ESyncState::HardDesync);
		bool bEverSoft = false;

		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			TickSolverHelper(Module,Solver);
			
			//kinematic desync will be known at end of frame because the simulation doesn't write results (so we know right away it's a desync)
			if(Step < LastStep)
			{
				EXPECT_EQ(PTKinematic->SyncState(),ESyncState::HardDesync);

				//islands merge and split depending on internal solve
				//but we should see dynamic being soft desync at least once when islands merge
				if(PTDynamic->SyncState() == ESyncState::SoftDesync)
				{
					bEverSoft = true;
				}
			}
			else
			{
				//everything in sync after last step
				EXPECT_EQ(PTKinematic->SyncState(),ESyncState::InSync);
				EXPECT_EQ(PTDynamic->SyncState(),ESyncState::InSync);				
			}

		}

		//kinematic hard desync, dynamic only soft desync
		const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
		EXPECT_EQ(DesyncedParticles.Num(),2);
		EXPECT_EQ(DesyncedParticles[0].MostDesynced,DesyncedParticles[0].Particle == Kinematic.Get() ? ESyncState::HardDesync : ESyncState::SoftDesync);
		EXPECT_EQ(DesyncedParticles[1].MostDesynced,DesyncedParticles[1].Particle == Kinematic.Get() ? ESyncState::HardDesync : ESyncState::SoftDesync);

		EXPECT_TRUE(bEverSoft);

		// We may end up a bit away from the surface (dt * V), due to solving for 0 velocity and not 0 position error
		EXPECT_GE(Dynamic->X()[2], 10);
		EXPECT_LE(Dynamic->X()[2], 12);

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits,RewindTest_SoftDesyncFromSameIslandThenBackToInSync)
	{
		if(TypeParam::IsRewindable() == false){ return; }
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(TVector<float,3>(0),10));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-100,-100,-10),FVec3(100,100,0)));

		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
		InitSolverSettings(Solver);

		Solver->EnableRewindCapture(100,true);	//soft desync only exists when resim optimization is on

		// Make particles
		auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();
		auto Kinematic = TKinematicGeometryParticle<float,3>::CreateParticle();

		Dynamic->SetGeometry(Sphere);
		Dynamic->SetGravityEnabled(true);
		Solver->RegisterObject(Dynamic.Get());
		Solver->GetEvolution()->GetGravityForces().SetAcceleration(FVec3(0,0,-1));

		Kinematic->SetGeometry(Box);
		Solver->RegisterObject(Kinematic.Get());

		Dynamic->SetX(FVec3(1000,0,37));
		Dynamic->SetGravityEnabled(true);
		Dynamic->SetObjectState(EObjectStateType::Dynamic);

		Kinematic->SetX(FVec3(0,0,0));

		ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Kinematic.Get()});

		const int32 LastStep = 15;

		TArray<FVec3> Xs;

		for(int Step = 0; Step <= LastStep; ++Step)
		{
			TickSolverHelper(Module,Solver);
			Xs.Add(Dynamic->X());
		}

		const int RewindStep = 0;

		FRewindData* RewindData = Solver->GetRewindData();
		EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

		//move kinematic very close but do not alter dynamic
		//should be soft desync while in island and then get back to in sync

		auto PTDynamic = static_cast<FSingleParticlePhysicsProxy<TPBDRigidParticle<FReal,3>>*>(Dynamic->GetProxy())->GetHandle();
		auto PTKinematic = static_cast<FSingleParticlePhysicsProxy<TKinematicGeometryParticle<FReal,3>>*>(Kinematic->GetProxy())->GetHandle();
		Kinematic->SetX(FVec3(1000-110,0,0));

		bool bEverSoft = false;

		for(int Step = RewindStep; Step <= LastStep; ++Step)
		{
			TickSolverHelper(Module,Solver);

			//kinematic desync will be known at end of frame because the simulation doesn't write results (so we know right away it's a desync)
			if(Step < LastStep)
			{
				EXPECT_EQ(PTKinematic->SyncState(),ESyncState::HardDesync);

				//islands merge and split depending on internal solve
				//but we should see dynamic being soft desync at least once when islands merge
				if(PTDynamic->SyncState() == ESyncState::SoftDesync)
				{
					bEverSoft = true;
				}

				//by end should be in sync because islands should definitely be split at this point
				if(Step == LastStep - 1)
				{
					EXPECT_EQ(PTDynamic->SyncState(),ESyncState::InSync);
				}
				
			}
			else
			{
				//everything in sync after last step
				EXPECT_EQ(PTKinematic->SyncState(),ESyncState::InSync);
				EXPECT_EQ(PTDynamic->SyncState(),ESyncState::InSync);
			}

		}

		//kinematic hard desync, dynamic only soft desync
		const TArray<FDesyncedParticleInfo> DesyncedParticles = RewindData->ComputeDesyncInfo();
		EXPECT_EQ(DesyncedParticles.Num(),2);
		EXPECT_EQ(DesyncedParticles[0].MostDesynced,DesyncedParticles[0].Particle == Kinematic.Get() ? ESyncState::HardDesync : ESyncState::SoftDesync);
		EXPECT_EQ(DesyncedParticles[1].MostDesynced,DesyncedParticles[1].Particle == Kinematic.Get() ? ESyncState::HardDesync : ESyncState::SoftDesync);

		//no collision so just kept falling
		EXPECT_LT(Dynamic->X()[2], 10);

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits, RewindTest_SoftDesyncFromSameIslandThenBackToInSync_GeometryCollection_SingleFallingUnderGravity)
	{
		if(TypeParam::IsRewindable() == false){ return; }

		for(int Optimization = 0; Optimization < 2; ++Optimization)
		{
			using Traits = TypeParam;
			TGeometryCollectionWrapper<Traits>* Collection = TNewSimulationObject<GeometryType::GeometryCollectionWithSingleRigid>::Init<Traits>()->As<TGeometryCollectionWrapper<Traits>>();

			TFramework<Traits> UnitTest;
			UnitTest.Solver->EnableRewindCapture(100,!!Optimization);
			UnitTest.AddSimulationObject(Collection);
			UnitTest.Initialize();

			TArray<FReal> Xs;
			const int32 LastStep = 10;
			for(int Step = 0; Step <= LastStep; ++Step)
			{
				UnitTest.Advance();
				Xs.Add(Collection->DynamicCollection->Transform[0].GetTranslation()[2]);
			}

			const int32 RewindStep = 3;

			FRewindData* RewindData = UnitTest.Solver->GetRewindData();
			EXPECT_TRUE(RewindData->RewindToFrame(RewindStep));

			//GC doesn't marshal data from GT to PT so at the moment all we get is the GT data immediately after rewind, but it doesn't make it over to PT or collection
			//Not sure if I can even access GT particle so can't verify that, but saw it in debugger at least
			
			for(int Step = RewindStep; Step <= LastStep; ++Step)
			{
				UnitTest.Advance();

				//TODO: turn this on when we find a way to marshal data from GT to PT
				//EXPECT_EQ(Collection->DynamicCollection->Transform[0].GetTranslation()[2],Xs[Step]);
			}			
		}
	}

	//Helps compare multiple runs for determinism
	//Also helps comparing runs across different compilers and delta times
	class FSimComparisonHelper
	{
	public:

		void SaveFrame(const TParticleView<TPBDRigidParticles<FReal, 3>>& NonDisabledDyanmic)
		{
			FEntry Frame;
			Frame.X.Reserve(NonDisabledDyanmic.Num());
			Frame.R.Reserve(NonDisabledDyanmic.Num());

			for(const auto& Dynamic : NonDisabledDyanmic)
			{
				Frame.X.Add(Dynamic.X());
				Frame.R.Add(Dynamic.R());
			}
			History.Add(MoveTemp(Frame));
		}

		static void ComputeMaxErrors(const FSimComparisonHelper& A, const FSimComparisonHelper& B, FReal& OutMaxLinearError,
			FReal& OutMaxAngularError, int32 HistoryMultiple=1)
		{
			ensure(B.History.Num() == (A.History.Num() * HistoryMultiple));
			
			FReal MaxLinearError2 = 0;
			FReal MaxAngularError2 = 0;

			for(int32 Idx = 0; Idx < A.History.Num(); ++Idx)
			{
				const int32 OtherIdx = Idx * HistoryMultiple  + (HistoryMultiple-1);
				const FEntry& Entry = A.History[Idx];
				const FEntry& OtherEntry = B.History[OtherIdx];

				FReal MaxLinearError,MaxAngularError;
				FEntry::CompareEntry(Entry,OtherEntry, MaxLinearError, MaxAngularError);

				MaxLinearError2 = FMath::Max(MaxLinearError2,MaxLinearError*MaxLinearError);
				MaxAngularError2 = FMath::Max(MaxAngularError2,MaxAngularError*MaxAngularError);
			}

			OutMaxLinearError = FMath::Sqrt(MaxLinearError2);
			OutMaxAngularError = FMath::Sqrt(MaxAngularError2);
		}

	private:
		struct FEntry
		{
			TArray<FVec3> X;
			TArray<FRotation3> R;

			static void CompareEntry(const FEntry& A, const FEntry& B, FReal& OutMaxLinearError, FReal& OutMaxAngularError)
			{
				FReal MaxLinearError2 = 0;
				FReal MaxAngularError2 = 0;

				check(A.X.Num() == A.R.Num());
				check(A.X.Num() == B.X.Num());
				for(int32 Idx = 0; Idx < A.X.Num(); ++Idx)
				{
					const FReal LinearError2 = (A.X[Idx] - B.X[Idx]).SizeSquared();
					MaxLinearError2 = FMath::Max(LinearError2,MaxLinearError2);

					//if exactly the same we want 0 for testing purposes, inverse does not get that so just skip it
					if(B.R[Idx] != A.R[Idx])
					{
						//For angular error we look at the rotation needed to go from B to A
						const FRotation3 Delta = B.R[Idx] * A.R[Idx].Inverse();

						FVec3 Axis;
						FReal Angle;
						Delta.ToAxisAndAngleSafe(Axis,Angle,FVec3(0,0,1));
						const FReal Angle2 = Angle*Angle;
						MaxAngularError2 = FMath::Max(Angle2,MaxAngularError2);
					}
				}

				OutMaxLinearError = FMath::Sqrt(MaxLinearError2);
				OutMaxAngularError = FMath::Sqrt(MaxAngularError2);
			}
		};

		TArray<FEntry> History;
	};

	template <typename TypeParam, typename InitLambda>
	void RunHelper(FSimComparisonHelper& SimComparison, int32 NumSteps, FReal Dt, const InitLambda& InitFunc)
	{
		FChaosSolversModule* Module = FChaosSolversModule::GetModule();

		// Make a solver
		auto* Solver = Module->CreateSolver<TypeParam>(nullptr);
		InitSolverSettings(Solver);

		TArray<TUniquePtr<TGeometryParticle<FReal, 3>>> Storage = InitFunc(Solver);

		for(int32 Step = 0; Step < NumSteps; ++Step)
		{
			TickSolverHelper(Module,Solver, Dt);
			SimComparison.SaveFrame(Solver->GetParticles().GetNonDisabledDynamicView());
		}

		Module->DestroySolver(Solver);
	}

	TYPED_TEST(AllTraits,DeterministicSim_SimpleFallingBox)
	{
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-10,-10,-10),FVec3(10,10,10)));

		const auto InitLambda = [&Box](auto& Solver)
		{
			TArray<TUniquePtr<TGeometryParticle<FReal,3>>> Storage;
			auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();

			Dynamic->SetGeometry(Box);
			Dynamic->SetGravityEnabled(true);
			Solver->RegisterObject(Dynamic.Get());
			Solver->GetEvolution()->GetGravityForces().SetAcceleration(FVec3(0,0,-1));
			Dynamic->SetObjectState(EObjectStateType::Dynamic);

			Storage.Add(MoveTemp(Dynamic));
			return Storage;
		};

		FSimComparisonHelper FirstRun;
		RunHelper<TypeParam>(FirstRun,100,1/30.f,InitLambda);

		FSimComparisonHelper SecondRun;
		RunHelper<TypeParam>(SecondRun,100,1/30.f,InitLambda);

		FReal MaxLinearError,MaxAngularError;
		FSimComparisonHelper::ComputeMaxErrors(FirstRun, SecondRun, MaxLinearError, MaxAngularError);
		EXPECT_EQ(MaxLinearError,0);
		EXPECT_EQ(MaxAngularError,0);
	}

	TYPED_TEST(AllTraits,DeterministicSim_ThresholdTest)
	{
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-10,-10,-10),FVec3(10,10,10)));

		FVec3 StartPos(0);
		FRotation3 StartRotation = FRotation3::FromIdentity();

		const auto InitLambda = [&Box, &StartPos, &StartRotation](auto& Solver)
		{
			TArray<TUniquePtr<TGeometryParticle<FReal,3>>> Storage;
			auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();

			Dynamic->SetGeometry(Box);
			Dynamic->SetGravityEnabled(true);
			Solver->RegisterObject(Dynamic.Get());
			Solver->GetEvolution()->GetGravityForces().SetAcceleration(FVec3(0,0,-1));
			Dynamic->SetObjectState(EObjectStateType::Dynamic);
			Dynamic->SetX(StartPos);
			Dynamic->SetR(StartRotation);

			Storage.Add(MoveTemp(Dynamic));
			return Storage;
		};

		FSimComparisonHelper FirstRun;
		RunHelper<TypeParam>(FirstRun,10,1/30.f,InitLambda);

		//move X within threshold
		StartPos = FVec3(0,0,1);

		FSimComparisonHelper SecondRun;
		RunHelper<TypeParam>(SecondRun,10,1/30.f,InitLambda);

		FReal MaxLinearError,MaxAngularError;
		FSimComparisonHelper::ComputeMaxErrors(FirstRun,SecondRun,MaxLinearError,MaxAngularError);
		EXPECT_EQ(MaxAngularError,0);
		EXPECT_LT(MaxLinearError,1.01);
		EXPECT_GT(MaxLinearError,0.99);

		//move R within threshold
		StartPos = FVec3(0,0,0);
		StartRotation = FRotation3::FromAxisAngle(FVec3(1,1,0).GetSafeNormal(),1);

		FSimComparisonHelper ThirdRun;
		RunHelper<TypeParam>(ThirdRun,10,1/30.f,InitLambda);

		FSimComparisonHelper::ComputeMaxErrors(FirstRun,ThirdRun,MaxLinearError,MaxAngularError);
		EXPECT_EQ(MaxLinearError,0);
		EXPECT_LT(MaxAngularError,1.01);
		EXPECT_GT(MaxAngularError,0.99);
	}

	TYPED_TEST(AllTraits,DeterministicSim_DoubleTick)
	{
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-10,-10,-10),FVec3(10,10,10)));

		const auto InitLambda = [&Box](auto& Solver)
		{
			TArray<TUniquePtr<TGeometryParticle<FReal,3>>> Storage;
			auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();

			Dynamic->SetGeometry(Box);
			Dynamic->SetGravityEnabled(false);
			Solver->RegisterObject(Dynamic.Get());
			Dynamic->SetObjectState(EObjectStateType::Dynamic);
			Dynamic->SetV(FVec3(1,0,0));

			Storage.Add(MoveTemp(Dynamic));
			return Storage;
		};

		FSimComparisonHelper FirstRun;
		RunHelper<TypeParam>(FirstRun,100,1/30.f,InitLambda);

		//tick twice as often

		FSimComparisonHelper SecondRun;
		RunHelper<TypeParam>(SecondRun,200,1/60.f,InitLambda);

		FReal MaxLinearError,MaxAngularError;
		FSimComparisonHelper::ComputeMaxErrors(FirstRun,SecondRun,MaxLinearError,MaxAngularError, 2);
		EXPECT_NEAR(MaxLinearError,0, 1e-4);
		EXPECT_NEAR(MaxAngularError,0,1e-4);
	}
	
	TYPED_TEST(AllTraits,DeterministicSim_DoubleTickGravity)
	{
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-10,-10,-10),FVec3(10,10,10)));
		const FReal Gravity = -980;

		const auto InitLambda = [&Box, Gravity](auto& Solver)
		{
			TArray<TUniquePtr<TGeometryParticle<FReal,3>>> Storage;
			auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();

			Dynamic->SetGeometry(Box);
			Dynamic->SetGravityEnabled(true);
			Solver->RegisterObject(Dynamic.Get());
			Solver->GetEvolution()->GetGravityForces().SetAcceleration(FVec3(0,0,Gravity));
			Dynamic->SetObjectState(EObjectStateType::Dynamic);

			Storage.Add(MoveTemp(Dynamic));
			return Storage;
		};

		const int32 NumSteps = 7;
		FSimComparisonHelper FirstRun;
		RunHelper<TypeParam>(FirstRun,NumSteps,1/30.f,InitLambda);

		//tick twice as often

		FSimComparisonHelper SecondRun;
		RunHelper<TypeParam>(SecondRun,NumSteps*2,1/60.f,InitLambda);

		//expected integration gravity error
		const auto EulerIntegrationHelper =[Gravity](int32 Steps, FReal Dt)
		{
			FReal Z = 0;
			FReal V = 0;
			for(int32 Step = 0; Step < Steps;++Step)
			{
				V += Gravity * Dt;
				Z += V * Dt;
			}

			return Z;
		};
		
		const FReal ExpectedZ30 = EulerIntegrationHelper(NumSteps,1/30.f);
		const FReal ExpectedZ60 = EulerIntegrationHelper(NumSteps*2,1/60.f);
		EXPECT_LT(ExpectedZ30,ExpectedZ60);	//30 gains speed faster (we use the end velocity to integrate so the bigger dt, the more added energy)
		const FReal ExpectedError = ExpectedZ60 - ExpectedZ30;

		FReal MaxLinearError,MaxAngularError;
		FSimComparisonHelper::ComputeMaxErrors(FirstRun,SecondRun,MaxLinearError,MaxAngularError, 2);
		EXPECT_LT(MaxLinearError,ExpectedError + 1e-4);
		EXPECT_EQ(MaxAngularError,0);
	}

	TYPED_TEST(AllTraits,DeterministicSim_DoubleTickCollide)
	{
		auto Sphere = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TSphere<FReal,3>(FVec3(0), 50));

		const auto InitLambda = [&Sphere](auto& Solver)
		{
			TArray<TUniquePtr<TGeometryParticle<FReal,3>>> Storage;
			auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();

			Dynamic->SetGeometry(Sphere);
			Solver->RegisterObject(Dynamic.Get());
			Dynamic->SetObjectState(EObjectStateType::Dynamic);
			Dynamic->SetGravityEnabled(false);
			Dynamic->SetV(FVec3(0,0,-25));


			auto Dynamic2 = TPBDRigidParticle<float,3>::CreateParticle();

			Dynamic2->SetGeometry(Sphere);
			Solver->RegisterObject(Dynamic2.Get());
			Dynamic2->SetX(FVec3(0,0,-100 -25/60.f-0.1));	//make it so it overlaps for 30fps but not 60
			Dynamic2->SetGravityEnabled(false);

			ChaosTest::SetParticleSimDataToCollide({Dynamic.Get(),Dynamic2.Get()});

			Storage.Add(MoveTemp(Dynamic));
			Storage.Add(MoveTemp(Dynamic2));

			return Storage;
		};

		const int32 NumSteps = 7;
		FSimComparisonHelper FirstRun;
		RunHelper<TypeParam>(FirstRun,NumSteps,1/30.f,InitLambda);

		//tick twice as often

		FSimComparisonHelper SecondRun;
		RunHelper<TypeParam>(SecondRun,NumSteps*2,1/60.f,InitLambda);

		FReal MaxLinearError,MaxAngularError;
		FSimComparisonHelper::ComputeMaxErrors(FirstRun,SecondRun,MaxLinearError,MaxAngularError,2);
	}

	TYPED_TEST(AllTraits,DeterministicSim_DoubleTickStackCollide)
	{
		auto SmallBox = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-50, -50, -50),FVec3(50, 50, 50)));
		auto Box = TSharedPtr<FImplicitObject,ESPMode::ThreadSafe>(new TBox<FReal,3>(FVec3(-1000, -1000, -1000),FVec3(1000, 1000, 0)));

		const auto InitLambda = [&SmallBox, &Box](auto& Solver)
		{
			Solver->GetEvolution()->GetGravityForces().SetAcceleration(FVec3(0,0,-980));
			TArray<TUniquePtr<TGeometryParticle<FReal,3>>> Storage;
			for(int Idx = 0; Idx < 5; ++Idx)
			{
				auto Dynamic = TPBDRigidParticle<float,3>::CreateParticle();

				Dynamic->SetGeometry(SmallBox);
				Solver->RegisterObject(Dynamic.Get());
				Dynamic->SetObjectState(EObjectStateType::Dynamic);
				Dynamic->SetGravityEnabled(true);
				Dynamic->SetX(FVec3(0,20*Idx,100*Idx));	//slightly offset

				Storage.Add(MoveTemp(Dynamic));
			}

			auto Kinematic = TKinematicGeometryParticle<float,3>::CreateParticle();

			Kinematic->SetGeometry(Box);
			Solver->RegisterObject(Kinematic.Get());
			Kinematic->SetX(FVec3(0,0,-50));

			Storage.Add(MoveTemp(Kinematic));

			for(int i = 0; i < Storage.Num(); ++i)
			{
				for(int j=i+1; j<Storage.Num(); ++j)
				{
					ChaosTest::SetParticleSimDataToCollide({Storage[i].Get(),Storage[j].Get()});
				}
			}

			return Storage;
		};

		const int32 NumSteps = 20;
		FSimComparisonHelper FirstRun;
		RunHelper<TypeParam>(FirstRun,NumSteps,1/30.f,InitLambda);

		//tick twice as often

		FSimComparisonHelper SecondRun;
		RunHelper<TypeParam>(SecondRun,NumSteps,1/30.f,InitLambda);

		//make sure deterministic
		FReal MaxLinearError,MaxAngularError;
		FSimComparisonHelper::ComputeMaxErrors(FirstRun,SecondRun,MaxLinearError,MaxAngularError,1);
		EXPECT_EQ(MaxLinearError,0);
		EXPECT_EQ(MaxAngularError,0);

		//try with 60fps
		FSimComparisonHelper ThirdRun;
		RunHelper<TypeParam>(ThirdRun,NumSteps*2,1/60.f,InitLambda);

		FSimComparisonHelper::ComputeMaxErrors(FirstRun,ThirdRun,MaxLinearError,MaxAngularError,2);
	}

}
