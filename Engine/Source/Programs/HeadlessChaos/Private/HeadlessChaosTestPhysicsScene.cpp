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
#include "Chaos/ChaosEngineInterface.h"
#include "Chaos/ChaosScene.h"
#include "SQAccelerator.h"
#include "CollisionQueryFilterCallbackCore.h"
#include "BodyInstanceCore.h"


namespace ChaosTest {

    using namespace Chaos;
	using namespace ChaosInterface;

	FSQHitBuffer<ChaosInterface::FOverlapHit> InSphereHelper(const FChaosScene& Scene, const FTransform& InTM, const FReal Radius)
	{
		FChaosSQAccelerator SQAccelerator(*Scene.GetSpacialAcceleration());
		FSQHitBuffer<ChaosInterface::FOverlapHit> HitBuffer;
		FOverlapAllQueryCallback QueryCallback;
		SQAccelerator.Overlap(TSphere<FReal,3>(FVec3(0),Radius),InTM,HitBuffer,FChaosQueryFilterData(),QueryCallback,FQueryDebugParams());
		return HitBuffer;
	}
	
	GTEST_TEST(EngineInterface, CreateAndReleaseActor)
	{
		FChaosScene Scene(nullptr);
		
		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}

		FChaosEngineInterface::ReleaseActor(Particle,&Scene);
		EXPECT_EQ(Particle,nullptr);		
	}

	GTEST_TEST(EngineInterface,CreateMoveAndReleaseInScene)
	{
		FChaosScene Scene(nullptr);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}

		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);

		//make sure acceleration structure has new actor right away
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),1);
		}

		//make sure acceleration structure sees moved actor right away
		const FTransform MovedTM(FQuat::Identity,FVec3(100,0,0));
		FChaosEngineInterface::SetGlobalPose_AssumesLocked(Particle,MovedTM);
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),0);

			const auto HitBuffer2 = InSphereHelper(Scene,MovedTM,3);
			EXPECT_EQ(HitBuffer2.GetNumHits(),1);
		}

		//move actor back and acceleration structure sees it right away
		FChaosEngineInterface::SetGlobalPose_AssumesLocked(Particle,FTransform::Identity);
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),1);
		}

		FChaosEngineInterface::ReleaseActor(Particle,&Scene);
		EXPECT_EQ(Particle,nullptr);

		//make sure acceleration structure no longer has actor
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),0);
		}

	}

	template <typename TSolver>
	void AdvanceSolverNoPushHelper(TSolver* Solver, float Dt)
	{
		Solver->AdvanceSolverBy(Dt);
	}

	GTEST_TEST(EngineInterface,AccelerationStructureHasSyncTimestamp)
	{
		//make sure acceleration structure has appropriate sync time

		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(),0);	//timestamp of 0 because we flush when scene is created

		FReal TotalDt = 0;
		for(int Step = 1; Step < 10; ++Step)
		{
			FVec3 Grav(0,0,-1);
			Scene.SetUpForFrame(&Grav, 1,99999,99999,10,false);
			Scene.StartFrame();
			Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();	//make sure we get a new tree every step
			Scene.EndFrame();

			EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(),Step);
		}
	}

	GTEST_TEST(EngineInterface,CreateActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}

		//tick solver but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0,0,-1);
			Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//create actor after structure is finished, but before swap happens
		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),1);
		}
	}

	GTEST_TEST(EngineInterface,MoveActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}

		//create actor before structure is ticked
		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);

		//tick solver so that particle is created, but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0,0,-1);
			Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//move object to get hit (shows pending move is applied)
		FChaosEngineInterface::SetGlobalPose_AssumesLocked(Particle,FTransform(FRotation3::FromIdentity(), FVec3(100,0,0)));
		
		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			TRigidTransform<FReal,3> OverlapTM(FVec3(100,0,0),FRotation3::FromIdentity());
			const auto HitBuffer = InSphereHelper(Scene,OverlapTM,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),1);
		}
	}

	GTEST_TEST(EngineInterface,RemoveActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}

		//create actor before structure is ticked
		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);

		//tick solver so that particle is created, but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0,0,-1);
			Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//delete object to get no hit
		FChaosEngineInterface::ReleaseActor(Particle, &Scene);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),0);
		}
	}

	GTEST_TEST(EngineInterface,RemoveActorPostFlush0Dt)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}

		//create actor before structure is ticked
		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);

		//tick solver so that particle is created, but don't call EndFrame (want to flush and swap manually)
		{
			//use 0 dt to make sure pending operations are not sensitive to 0 dt
			FVec3 Grav(0,0,-1);
			Scene.SetUpForFrame(&Grav,0,99999,99999,10,false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//delete object to get no hit
		FChaosEngineInterface::ReleaseActor(Particle,&Scene);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),0);
		}
	}

	GTEST_TEST(EngineInterface,CreateAndRemoveActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		//tick solver, but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0,0,-1);
			Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}

		//create actor after flush
		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);

		//delete object right away to get no hit
		FChaosEngineInterface::ReleaseActor(Particle,&Scene);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
			EXPECT_EQ(HitBuffer.GetNumHits(),0);
		}
	}

	GTEST_TEST(EngineInterface,CreateDelayed)
	{
		for(int Delay = 0; Delay < 4; ++Delay)
		{
			FChaosScene Scene(nullptr);
			Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
			Scene.GetSolver()->GetMarshallingManager().SetTickDelay_External(Delay);

			FActorCreationParams Params;
			Params.Scene = &Scene;

			TGeometryParticle<FReal,3>* Particle = nullptr;

			FChaosEngineInterface::CreateActor(Params,Particle);
			EXPECT_NE(Particle,nullptr);

			{
				auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
				Particle->SetGeometry(MoveTemp(Sphere));
			}

			//create actor after flush
			TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
			Scene.AddActorsToScene_AssumesLocked(Particles);

			for(int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				//tick solver
				{
					FVec3 Grav(0,0,-1);
					Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
					Scene.StartFrame();
					Scene.EndFrame();
				}

				//make sure sim hasn't seen it yet
				{
					FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
					const auto& SOA = Evolution->GetParticles();
					EXPECT_EQ(SOA.GetAllParticlesView().Num(),0);
				}

				//make sure external thread knows about it
				{
					const auto HitBuffer = InSphereHelper(Scene,FTransform::Identity,3);
					EXPECT_EQ(HitBuffer.GetNumHits(),1);
				}
			}

			//tick solver one last time
			{
				FVec3 Grav(0,0,-1);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				Scene.EndFrame();
			}

			//now sim knows about it
			{
				FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
				const auto& SOA = Evolution->GetParticles();
				EXPECT_EQ(SOA.GetAllParticlesView().Num(),1);
			}

			Particle->SetX(FVec3(5,0,0));

			for(int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				//tick solver
				{
					FVec3 Grav(0,0,-1);
					Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
					Scene.StartFrame();
					Scene.EndFrame();
				}

				//make sure sim hasn't seen new X yet
				{
					FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
					const auto& SOA = Evolution->GetParticles();
					const auto& InternalParticle = *SOA.GetAllParticlesView().Begin();
					EXPECT_EQ(InternalParticle.X()[0],0);
				}
			}

			//tick solver one last time
			{
				FVec3 Grav(0,0,-1);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				Scene.EndFrame();
			}

			//now sim knows about new X
			{
				FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
				const auto& SOA = Evolution->GetParticles();
				const auto& InternalParticle = *SOA.GetAllParticlesView().Begin();
				EXPECT_EQ(InternalParticle.X()[0],5);
			}

			//make sure commands are also deferred

			int Count = 0;
			int ExternalCount = 0;
			const auto Lambda = [&]()
			{
				++Count;
				EXPECT_EQ(Count,1);	//only hit once on internal thread
				EXPECT_EQ(ExternalCount,Delay); //internal hits with expected delay
			};

			Scene.GetSolver()->EnqueueCommandImmediate(Lambda);

			for(int Repeat = 0; Repeat < Delay+1; ++Repeat)
			{
				//tick solver
				FVec3 Grav(0,0,-1);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				Scene.EndFrame();

				++ExternalCount;
			}

		}
		
	}

	GTEST_TEST(EngineInterface,RemoveDelayed)
	{
		for(int Delay = 0; Delay < 4; ++Delay)
		{
			FChaosScene Scene(nullptr);
			Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
			Scene.GetSolver()->GetMarshallingManager().SetTickDelay_External(Delay);

			FActorCreationParams Params;
			Params.Scene = &Scene;

			Params.bSimulatePhysics = true;	//simulate so that sync body is triggered
			Params.bStartAwake = true;

			TGeometryParticle<FReal,3>* Particle = nullptr;
			FChaosEngineInterface::CreateActor(Params,Particle);
			EXPECT_NE(Particle,nullptr);

			{
				auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
				Particle->SetGeometry(MoveTemp(Sphere));
				auto Simulated = static_cast<TKinematicGeometryParticle<FReal,3>*>(Particle);
				Simulated->SetV(FVec3(0,0,-1));
			}


			//make second simulating particle that we don't delete. Needed to trigger a sync
			//this is because some data is cleaned up on GT immediately
			TGeometryParticle<FReal,3>* Particle2 = nullptr;
			FChaosEngineInterface::CreateActor(Params,Particle2);	
			EXPECT_NE(Particle2,nullptr);
			{
				auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
				Particle2->SetGeometry(MoveTemp(Sphere));
				auto Simulated = static_cast<TKinematicGeometryParticle<FReal,3>*>(Particle2);
				Simulated->SetV(FVec3(0,-1,0));
			}

			//create actor
			TArray<TGeometryParticle<FReal,3>*> Particles ={Particle, Particle2};
			Scene.AddActorsToScene_AssumesLocked(Particles);

			//tick until it's being synced from sim
			for(int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0,0,0);
					Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
					Scene.StartFrame();
					Scene.EndFrame();
				}
			}

			//x starts at 0
			EXPECT_NEAR(Particle->X()[2],0, 1e-4);
			EXPECT_NEAR(Particle2->X()[1],0, 1e-4);

			//tick solver and see new position synced from sim
			{
				FVec3 Grav(0,0,0);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle->X()[2],-1, 1e-4);
				EXPECT_NEAR(Particle2->X()[1],-1, 1e-4);
			}

			//tick solver and delete in between solver finishing and sync
			{
				FVec3 Grav(0,0,0);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();

				//delete particle
				FChaosEngineInterface::ReleaseActor(Particle,&Scene);

				Scene.EndFrame();
				EXPECT_NEAR(Particle2->X()[1],-2, 1e-4);	//other particle keeps moving
			}


			//tick again and don't crash
			for(int Repeat = 0; Repeat < Delay + 1; ++Repeat)
			{
				{
					FVec3 Grav(0,0,0);
					Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
					Scene.StartFrame();
					Scene.EndFrame();
					EXPECT_NEAR(Particle2->X()[1],-3 - Repeat, 1e-4);	//other particle keeps moving
				}
			}
		}
	}

	GTEST_TEST(EngineInterface,MoveDelayed)
	{
		for(int Delay = 0; Delay < 4; ++Delay)
		{
			FChaosScene Scene(nullptr);
			Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
			Scene.GetSolver()->GetMarshallingManager().SetTickDelay_External(Delay);

			FActorCreationParams Params;
			Params.Scene = &Scene;

			Params.bSimulatePhysics = true;	//simulated so that gt conflicts with sim thread
			Params.bStartAwake = true;

			TGeometryParticle<FReal,3>* Particle = nullptr;
			FChaosEngineInterface::CreateActor(Params,Particle);
			EXPECT_NE(Particle,nullptr);

			{
				auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
				Particle->SetGeometry(MoveTemp(Sphere));
				auto Simulated = static_cast<TKinematicGeometryParticle<FReal,3>*>(Particle);
				Simulated->SetV(FVec3(0,0,-1));
			}

			//create actor
			TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
			Scene.AddActorsToScene_AssumesLocked(Particles);

			//tick until it's being synced from sim
			for(int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0,0,0);
					Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
					Scene.StartFrame();
					Scene.EndFrame();
				}
			}

			//x starts at 0
			EXPECT_NEAR(Particle->X()[2],0,1e-4);

			//tick solver and see new position synced from sim
			{
				FVec3 Grav(0,0,0);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle->X()[2],-1,1e-4);
			}

			//set new x position and make sure we see it right away even though there's delay
			FChaosEngineInterface::SetGlobalPose_AssumesLocked(Particle,FTransform(FQuat::Identity,FVec3(0,0,10)));

			for(int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0,0,0);
					Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
					Scene.StartFrame();
					Scene.EndFrame();

					EXPECT_NEAR(Particle->X()[2],10,1e-4);	//until we catch up, just use GT data
				}
			}

			//tick solver one last time, should see sim results from the place we teleported to
			{
				FVec3 Grav(0,0,0);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle->X()[2],9,1e-4);
			}

			//set x after sim but before EndFrame, make sure to see gt position since it was written after
			{
				FVec3 Grav(0,0,0);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				FChaosEngineInterface::SetGlobalPose_AssumesLocked(Particle,FTransform(FQuat::Identity,FVec3(0,0,100)));
				Scene.EndFrame();
				EXPECT_NEAR(Particle->X()[2],100,1e-4);
			}

			for(int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0,0,0);
					Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
					Scene.StartFrame();
					Scene.EndFrame();

					EXPECT_NEAR(Particle->X()[2],100,1e-4);	//until we catch up, just use GT data
				}
			}

			//tick solver one last time, should see sim results from the place we teleported to
			{
				FVec3 Grav(0,0,0);
				Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle->X()[2],99,1e-4);
			}
		}
	}

	GTEST_TEST(EngineInterface, SimRoundTrip)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);

		{
			auto Sphere = MakeUnique<TSphere<FReal,3>>(FVec3(0),3);
			Particle->SetGeometry(MoveTemp(Sphere));
		}
		TPBDRigidParticle<FReal,3>* Simulated = static_cast<TPBDRigidParticle<FReal,3>*>(Particle);

		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);
		Simulated->SetObjectState(EObjectStateType::Dynamic);
		Simulated->SetF(FVec3(0,0,10) * Simulated->M());

		FVec3 Grav(0,0,0);
		Scene.SetUpForFrame(&Grav,1,99999,99999,10,false);
		Scene.StartFrame();
		Scene.EndFrame();

		//integration happened and we get results back
		EXPECT_EQ(Simulated->X(),FVec3(0,0,10));
		EXPECT_EQ(Simulated->V(),FVec3(0,0,10));

	}
}
