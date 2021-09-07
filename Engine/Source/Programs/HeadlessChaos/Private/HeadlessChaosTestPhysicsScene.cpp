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

namespace Chaos
{
	extern CHAOS_API float AsyncInterpolationMultiplier;
}

namespace ChaosTest {

	using namespace Chaos;
	using namespace ChaosInterface;

	FSQHitBuffer<ChaosInterface::FOverlapHit> InSphereHelper(const FChaosScene& Scene, const FTransform& InTM, const FReal Radius)
	{
		FChaosSQAccelerator SQAccelerator(*Scene.GetSpacialAcceleration());
		FSQHitBuffer<ChaosInterface::FOverlapHit> HitBuffer;
		FOverlapAllQueryCallback QueryCallback;
		SQAccelerator.Overlap(TSphere<FReal, 3>(FVec3(0), Radius), InTM, HitBuffer, FChaosQueryFilterData(), QueryCallback, FQueryDebugParams());
		return HitBuffer;
	}

	GTEST_TEST(EngineInterface, CreateAndReleaseActor)
	{
		FChaosScene Scene(nullptr);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Proxy->GetGameThreadAPI().SetGeometry(MoveTemp(Sphere));
		}

		FChaosEngineInterface::ReleaseActor(Proxy, &Scene);
		EXPECT_EQ(Proxy, nullptr);
	}

	GTEST_TEST(EngineInterface, CreateMoveAndReleaseInScene)
	{
		FChaosScene Scene(nullptr);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		//make sure acceleration structure has new actor right away
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 1);
		}

		//make sure acceleration structure sees moved actor right away
		const FTransform MovedTM(FQuat::Identity, FVec3(100, 0, 0));
		FChaosEngineInterface::SetGlobalPose_AssumesLocked(Proxy, MovedTM);
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 0);

			const auto HitBuffer2 = InSphereHelper(Scene, MovedTM, 3);
			EXPECT_EQ(HitBuffer2.GetNumHits(), 1);
		}

		//move actor back and acceleration structure sees it right away
		FChaosEngineInterface::SetGlobalPose_AssumesLocked(Proxy, FTransform::Identity);
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 1);
		}

		FChaosEngineInterface::ReleaseActor(Proxy, &Scene);
		EXPECT_EQ(Proxy, nullptr);

		//make sure acceleration structure no longer has actor
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 0);
		}

	}

	template <typename TSolver>
	void AdvanceSolverNoPushHelper(TSolver* Solver, FReal Dt)
	{
		Solver->AdvanceSolverBy(Dt);
	}

	GTEST_TEST(EngineInterface, AccelerationStructureHasSyncTimestamp)
	{
		//make sure acceleration structure has appropriate sync time

		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 0);	//timestamp of 0 because we flush when scene is created

		FReal TotalDt = 0;
		for (int Step = 1; Step < 10; ++Step)
		{
			FVec3 Grav(0, 0, -1);
			Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
			Scene.StartFrame();
			Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();	//make sure we get a new tree every step
			Scene.EndFrame();

			EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), Step);
		}
	}

	GTEST_TEST(EngineInterface, AccelerationStructureHasSyncTimestamp_MultiFrameDelay)
	{
		//make sure acceleration structure has appropriate sync time when PT falls behind GT

		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		Scene.GetSolver()->SetStealAdvanceTasks_ForTesting(true); // prevents execution on StartFrame so we can execute task manually.

		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 0);	//timestamp of 0 because we flush when scene is created

		FVec3 Grav(0, 0, -1);
		Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);

		// Game thread enqueues second solver task before first completes (we did not execute advance task)
		Scene.StartFrame();
		Scene.EndFrame();
		Scene.StartFrame();

		// Execute first enqueued advance task
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		// No EndFrame called after PT execution, stamp should still be 0.
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 0);

		// Endframe update structure to stamp 1, as we have completed 1 frame on PT.
		Scene.EndFrame();
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 1);

		Scene.StartFrame();

		// PT catches up during this frame
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();
		Scene.EndFrame();

		// New structure should be at 3 as PT/GT are in sync.
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 3);

	}

	GTEST_TEST(EngineInterface, AccelerationStructureHasSyncTimestamp_MultiFrameDelay2)
	{
		//make sure acceleration structure has appropriate sync time when PT falls behind GT

		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		Scene.GetSolver()->SetStealAdvanceTasks_ForTesting(true); // prevents execution on StartFrame so we can execute task manually.

		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 0);	//timestamp of 0 because we flush when scene is created

		FVec3 Grav(0, 0, -1);
		Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);

		// PT not finished yet (we didn't execute solver task), should still be 0.
		Scene.StartFrame();
		Scene.EndFrame();
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 0);

		// PT not finished yet (we didn't execute solver task), should still be 0.
		Scene.StartFrame();
		Scene.EndFrame();
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 0);

		// First PT task finished this frame, we are two behind, now at time 1.
		Scene.StartFrame();
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();
		Scene.EndFrame();
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 1);

		// Remaining two PT tasks finish, we are caught up, but still time 1 as EndFrame has not updated our structure.
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 1);

		// PT task this frame finishes before EndFrame, putting us at 4, in sync with GT.
		Scene.StartFrame();
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();
		Scene.EndFrame();
		EXPECT_EQ(Scene.GetSpacialAcceleration()->GetSyncTimestamp(), 4);
	}

	GTEST_TEST(EngineInterface, PullFromPhysicsState_MultiFrameDelay)
	{
		// This test is designed to verify pulldata is being timestamped correctly, and that we will not write to a deleted GT Proxy 
		// in this case. 

		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		Scene.GetSolver()->SetStealAdvanceTasks_ForTesting(true); // prevents execution on StartFrame so we can execute task manually.

		FVec3 Grav(0, 0, -1);
		Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);

		FActorCreationParams Params;
		Params.Scene = &Scene;
		Params.bSimulatePhysics = true;
		Params.bEnableGravity = true;
		Params.bStartAwake = true;


		// Create two Proxys, one to remove for test, the other to ensure we have > 0 proxies to hit the pull physics data path.
		FPhysicsActorHandle Proxy = nullptr;
		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}
		FPhysicsActorHandle Proxy2 = nullptr;
		FChaosEngineInterface::CreateActor(Params, Proxy2);
		auto& Particle2 = Proxy2->GetGameThreadAPI();
		EXPECT_NE(Proxy2, nullptr);
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle2.SetGeometry(MoveTemp(Sphere));
		}
		TArray<FPhysicsActorHandle> Proxys = { Proxy, Proxy2 };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		// verify external timestamps are as expected.
		auto& MarshallingManager = Scene.GetSolver()->GetMarshallingManager();
		EXPECT_EQ(MarshallingManager.GetExternalTimestamp_External(), 1);

		// Execute a frame such that Proxys should be initialized in physics thread and game thread.
		Scene.StartFrame();
		EXPECT_EQ(MarshallingManager.GetExternalTimestamp_External(), 2);
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.EndFrame();

		// run GT frame, no PT task executed.
		Scene.StartFrame();
		EXPECT_EQ(MarshallingManager.GetExternalTimestamp_External(), 3);
		Scene.EndFrame();

		// enqueue another frame.
		Scene.StartFrame();
		EXPECT_EQ(MarshallingManager.GetExternalTimestamp_External(), 4);

		// Remove Proxy, is stamped with external time 4. PT needs to run 3 frames before this will be removed,
		// as we are two PT tasks behind, and this has not been enqueued yet.
		auto StaleProxy = Proxy;
		FChaosEngineInterface::ReleaseActor(Proxy, &Scene);
		EXPECT_EQ(Proxy, nullptr);
		EXPECT_EQ(StaleProxy->GetSyncTimestamp()->bDeleted, true);

		// Run PT task for internal timestamp 2.
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();

		// Proxy should not get touched in Pull, as timestamp from removal should be greater than pulldata timestamp. (4 > 2).
		// (if it was touched we'd crash as it is now deleted).
		Scene.EndFrame();


		Scene.StartFrame();
		EXPECT_EQ(MarshallingManager.GetExternalTimestamp_External(), 5);
		EXPECT_EQ(StaleProxy->GetSyncTimestamp()->bDeleted, true);

		// run pt task for internal timestamp 3. Proxy still not removed on PT.
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		EXPECT_EQ(Scene.GetSolver()->GetEvolution()->GetParticles().GetAllParticlesView().Num(), 2); // none have been removed on pt, still 2 Proxys.

		// Proxy should not get touched in pull, as timestamp from removal is less than pulldata timestamp (3 < 4)
		// If this crashes in pull, that means this test has regressed. (Pulldata timestamp is likely wrong).
		Scene.EndFrame();


		Scene.StartFrame();
		EXPECT_EQ(MarshallingManager.GetExternalTimestamp_External(), 6);
		EXPECT_EQ(StaleProxy->GetSyncTimestamp()->bDeleted, true);
		EXPECT_EQ(Scene.GetSolver()->GetEvolution()->GetParticles().GetAllParticlesView().Num(), 2); // Proxys not yet removed on pt, still 2.


		// This is PT task that should remove Proxy (internal timestamp 4, matching stamp on removed Proxy's dirty data).
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		EXPECT_EQ(Scene.GetSolver()->GetEvolution()->GetParticles().GetAllParticlesView().Num(), 1); // one Proxy removed on pt, one remaining.

		// This PT task catches up to gamethread.
		Scene.GetSolver()->PopAndExecuteStolenAdvanceTask_ForTesting();
		Scene.EndFrame();
	}




	GTEST_TEST(EngineInterface, CreateActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		//tick solver but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0, 0, -1);
			Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//create actor after structure is finished, but before swap happens
		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 1);
		}
	}

	GTEST_TEST(EngineInterface, MoveActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		//create actor before structure is ticked
		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		//tick solver so that Proxy is created, but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0, 0, -1);
			Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//move object to get hit (shows pending move is applied)
		FChaosEngineInterface::SetGlobalPose_AssumesLocked(Proxy, FTransform(FRotation3::FromIdentity(), FVec3(100, 0, 0)));

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			TRigidTransform<FReal, 3> OverlapTM(FVec3(100, 0, 0), FRotation3::FromIdentity());
			const auto HitBuffer = InSphereHelper(Scene, OverlapTM, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 1);
		}
	}

	GTEST_TEST(EngineInterface, RemoveActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		//create actor before structure is ticked
		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		//tick solver so that Proxy is created, but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0, 0, -1);
			Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//delete object to get no hit
		FChaosEngineInterface::ReleaseActor(Proxy, &Scene);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 0);
		}
	}

	GTEST_TEST(EngineInterface, RemoveActorPostFlush0Dt)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		//create actor before structure is ticked
		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		//tick solver so that Proxy is created, but don't call EndFrame (want to flush and swap manually)
		{
			//use 0 dt to make sure pending operations are not sensitive to 0 dt
			FVec3 Grav(0, 0, -1);
			Scene.SetUpForFrame(&Grav, 0, 99999, 99999, 10, false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		//delete object to get no hit
		FChaosEngineInterface::ReleaseActor(Proxy, &Scene);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 0);
		}
	}

	GTEST_TEST(EngineInterface, CreateAndRemoveActorPostFlush)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		//tick solver, but don't call EndFrame (want to flush and swap manually)
		{
			FVec3 Grav(0, 0, -1);
			Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
			Scene.StartFrame();
		}

		//make sure acceleration structure is built
		Scene.GetSolver()->GetEvolution()->FlushSpatialAcceleration();

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		EXPECT_NE(Proxy, nullptr);

		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		//create actor after flush
		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		//delete object right away to get no hit
		FChaosEngineInterface::ReleaseActor(Proxy, &Scene);

		Scene.CopySolverAccelerationStructure();	//trigger swap manually and see pending changes apply
		{
			const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
			EXPECT_EQ(HitBuffer.GetNumHits(), 0);
		}
	}

	GTEST_TEST(EngineInterface, CreateDelayed)
	{
		for (int Delay = 0; Delay < 4; ++Delay)
		{
			FChaosScene Scene(nullptr);
			Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
			Scene.GetSolver()->GetMarshallingManager().SetTickDelay_External(Delay);

			FActorCreationParams Params;
			Params.Scene = &Scene;

			FPhysicsActorHandle Proxy = nullptr;

			FChaosEngineInterface::CreateActor(Params, Proxy);
			auto& Particle = Proxy->GetGameThreadAPI();
			EXPECT_NE(Proxy, nullptr);

			{
				auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
				Particle.SetGeometry(MoveTemp(Sphere));
			}

			//create actor after flush
			TArray<FPhysicsActorHandle> Proxys = { Proxy };
			Scene.AddActorsToScene_AssumesLocked(Proxys);

			for (int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				//tick solver
				{
					FVec3 Grav(0, 0, -1);
					Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
					Scene.StartFrame();
					Scene.EndFrame();
				}

				//make sure sim hasn't seen it yet
				{
					FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
					const auto& SOA = Evolution->GetParticles();
					EXPECT_EQ(SOA.GetAllParticlesView().Num(), 0);
				}

				//make sure external thread knows about it
				{
					const auto HitBuffer = InSphereHelper(Scene, FTransform::Identity, 3);
					EXPECT_EQ(HitBuffer.GetNumHits(), 1);
				}
			}

			//tick solver one last time
			{
				FVec3 Grav(0, 0, -1);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				Scene.EndFrame();
			}

			//now sim knows about it
			{
				FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
				const auto& SOA = Evolution->GetParticles();
				EXPECT_EQ(SOA.GetAllParticlesView().Num(), 1);
			}

			Particle.SetX(FVec3(5, 0, 0));

			for (int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				//tick solver
				{
					FVec3 Grav(0, 0, -1);
					Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
					Scene.StartFrame();
					Scene.EndFrame();
				}

				//make sure sim hasn't seen new X yet
				{
					FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
					const auto& SOA = Evolution->GetParticles();
					const auto& InternalProxy = *SOA.GetAllParticlesView().Begin();
					EXPECT_EQ(InternalProxy.X()[0], 0);
				}
			}

			//tick solver one last time
			{
				FVec3 Grav(0, 0, -1);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				Scene.EndFrame();
			}

			//now sim knows about new X
			{
				FPBDRigidsEvolution* Evolution = Scene.GetSolver()->GetEvolution();
				const auto& SOA = Evolution->GetParticles();
				const auto& InternalProxy = *SOA.GetAllParticlesView().Begin();
				EXPECT_EQ(InternalProxy.X()[0], 5);
			}

			//make sure commands are also deferred

			int Count = 0;
			int ExternalCount = 0;
			TUniqueFunction<void()> Lambda = [&]()
			{
				++Count;
				EXPECT_EQ(Count, 1);	//only hit once on internal thread
				EXPECT_EQ(ExternalCount, Delay); //internal hits with expected delay
			};

			Scene.GetSolver()->EnqueueCommandImmediate(Lambda);

			for (int Repeat = 0; Repeat < Delay + 1; ++Repeat)
			{
				//tick solver
				FVec3 Grav(0, 0, -1);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				Scene.EndFrame();

				++ExternalCount;
			}

		}

	}

	GTEST_TEST(EngineInterface, RemoveDelayed)
	{
		for (int Delay = 0; Delay < 4; ++Delay)
		{
			FChaosScene Scene(nullptr);
			Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
			Scene.GetSolver()->GetMarshallingManager().SetTickDelay_External(Delay);

			FActorCreationParams Params;
			Params.Scene = &Scene;

			Params.bSimulatePhysics = true;	//simulate so that sync body is triggered
			Params.bStartAwake = true;

			FPhysicsActorHandle Proxy = nullptr;
			FChaosEngineInterface::CreateActor(Params, Proxy);
			auto& Particle = Proxy->GetGameThreadAPI();
			EXPECT_NE(Proxy, nullptr);

			{
				auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
				Particle.SetGeometry(MoveTemp(Sphere));
				Particle.SetV(FVec3(0, 0, -1));
			}


			//make second simulating Proxy that we don't delete. Needed to trigger a sync
			//this is because some data is cleaned up on GT immediately
			FPhysicsActorHandle Proxy2 = nullptr;
			FChaosEngineInterface::CreateActor(Params, Proxy2);
			auto& Particle2 = Proxy2->GetGameThreadAPI();
			EXPECT_NE(Proxy2, nullptr);
			{
				auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
				Particle2.SetGeometry(MoveTemp(Sphere));
				Particle2.SetV(FVec3(0, -1, 0));
			}

			//create actor
			TArray<FPhysicsActorHandle> Proxys = { Proxy, Proxy2 };
			Scene.AddActorsToScene_AssumesLocked(Proxys);

			//tick until it's being synced from sim
			for (int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0, 0, 0);
					Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
					Scene.StartFrame();
					Scene.EndFrame();
				}
			}

			//x starts at 0
			EXPECT_NEAR(Particle.X()[2], 0, 1e-4);
			EXPECT_NEAR(Particle2.X()[1], 0, 1e-4);

			//tick solver and see new position synced from sim
			{
				FVec3 Grav(0, 0, 0);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle.X()[2], -1, 1e-4);
				EXPECT_NEAR(Particle2.X()[1], -1, 1e-4);
			}

			//tick solver and delete in between solver finishing and sync
			{
				FVec3 Grav(0, 0, 0);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();

				//delete Proxy
				FChaosEngineInterface::ReleaseActor(Proxy, &Scene);

				Scene.EndFrame();
				EXPECT_NEAR(Particle2.X()[1], -2, 1e-4);	//other Proxy keeps moving
			}


			//tick again and don't crash
			for (int Repeat = 0; Repeat < Delay + 1; ++Repeat)
			{
				{
					FVec3 Grav(0, 0, 0);
					Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
					Scene.StartFrame();
					Scene.EndFrame();
					EXPECT_NEAR(Particle2.X()[1], -3 - Repeat, 1e-4);	//other Proxy keeps moving
				}
			}
		}
	}

	GTEST_TEST(EngineInterface, MoveDelayed)
	{
		for (int Delay = 0; Delay < 4; ++Delay)
		{
			FChaosScene Scene(nullptr);
			Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
			Scene.GetSolver()->GetMarshallingManager().SetTickDelay_External(Delay);

			FActorCreationParams Params;
			Params.Scene = &Scene;

			Params.bSimulatePhysics = true;	//simulated so that gt conflicts with sim thread
			Params.bStartAwake = true;

			FPhysicsActorHandle Proxy = nullptr;
			FChaosEngineInterface::CreateActor(Params, Proxy);
			auto& Particle = Proxy->GetGameThreadAPI();
			EXPECT_NE(Proxy, nullptr);

			{
				auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
				Particle.SetGeometry(MoveTemp(Sphere));
				Particle.SetV(FVec3(0, 0, -1));
			}

			//create actor
			TArray<FPhysicsActorHandle> Proxys = { Proxy };
			Scene.AddActorsToScene_AssumesLocked(Proxys);

			//tick until it's being synced from sim
			for (int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0, 0, 0);
					Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
					Scene.StartFrame();
					Scene.EndFrame();
				}
			}

			//x starts at 0
			EXPECT_NEAR(Particle.X()[2], 0, 1e-4);

			//tick solver and see new position synced from sim
			{
				FVec3 Grav(0, 0, 0);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle.X()[2], -1, 1e-4);
			}

			//set new x position and make sure we see it right away even though there's delay
			FChaosEngineInterface::SetGlobalPose_AssumesLocked(Proxy, FTransform(FQuat::Identity, FVec3(0, 0, 10)));

			for (int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0, 0, 0);
					Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
					Scene.StartFrame();
					Scene.EndFrame();

					EXPECT_NEAR(Particle.X()[2], 10, 1e-4);	//until we catch up, just use GT data
				}
			}

			//tick solver one last time, should see sim results from the place we teleported to
			{
				FVec3 Grav(0, 0, 0);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle.X()[2], 9, 1e-4);
			}

			//set x after sim but before EndFrame, make sure to see gt position since it was written after
			{
				FVec3 Grav(0, 0, 0);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				FChaosEngineInterface::SetGlobalPose_AssumesLocked(Proxy, FTransform(FQuat::Identity, FVec3(0, 0, 100)));
				Scene.EndFrame();
				EXPECT_NEAR(Particle.X()[2], 100, 1e-4);
			}

			for (int Repeat = 0; Repeat < Delay; ++Repeat)
			{
				{
					FVec3 Grav(0, 0, 0);
					Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
					Scene.StartFrame();
					Scene.EndFrame();

					EXPECT_NEAR(Particle.X()[2], 100, 1e-4);	//until we catch up, just use GT data
				}
			}

			//tick solver one last time, should see sim results from the place we teleported to
			{
				FVec3 Grav(0, 0, 0);
				Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
				Scene.StartFrame();
				Scene.EndFrame();
				EXPECT_NEAR(Particle.X()[2], 99, 1e-4);
			}
		}
	}

	GTEST_TEST(EngineInterface, SimRoundTrip)
	{
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);
		Particle.SetObjectState(EObjectStateType::Dynamic);
		Particle.AddForce(FVec3(0, 0, 10) * Particle.M());

		FVec3 Grav(0, 0, 0);
		Scene.SetUpForFrame(&Grav, 1, 99999, 99999, 10, false);
		Scene.StartFrame();
		Scene.EndFrame();

		//integration happened and we get results back
		EXPECT_EQ(Particle.X(), FVec3(0, 0, 10));
		EXPECT_EQ(Particle.V(), FVec3(0, 0, 10));

	}

	GTEST_TEST(EngineInterface, SimInterpolated)
	{
		//Need to test:
		//position interpolation
		//position interpolation from an inactive Proxy (i.e a step function)
		//position interpolation from an active to an inactive Proxy (i.e a step function but reversed)
		//interpolation to a deleted Proxy
		//state change should be a step function (sleep state)
		//wake events must be collapsed (sleep awake sleep becomes sleep)
		//collision events must be collapsed
		//forces are averaged
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		const FReal FixedDT = 1;
		Scene.GetSolver()->EnableAsyncMode(1);	//tick 1 dt at a time

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;
		FPhysicsActorHandle Proxy2 = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		FChaosEngineInterface::CreateActor(Params, Proxy2);
		auto& Particle2 = Proxy2->GetGameThreadAPI();
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle2.SetGeometry(MoveTemp(Sphere));
		}

		TArray<FPhysicsActorHandle> Proxys = { Proxy, Proxy2 };
		Scene.AddActorsToScene_AssumesLocked(Proxys);
		Particle.SetObjectState(EObjectStateType::Dynamic);
		const FReal ZVel = 10;
		const FReal ZStart = 100;
		const FVec3 ConstantForce(0, 0, 1 * Particle2.M());
		Particle.SetV(FVec3(0, 0, ZVel));
		Particle.SetX(FVec3(0, 0, ZStart));
		const int32 NumGTSteps = 24;
		const int32 NumPTSteps = 24 / 4;

		struct FCallback : public TSimCallbackObject<FSimCallbackNoInput>
		{
			virtual void OnPreSimulate_Internal() override
			{
				EXPECT_EQ(GetConsumerInput_Internal(), nullptr);	//no inputs passed in
				//we expect the dt to be 1
				EXPECT_EQ(GetDeltaTime_Internal(), 1);
				EXPECT_EQ(GetSimTime_Internal(), Count);
				Count++;
			}

			int32 Count = 0;

			int32 NumPTSteps;
		};

		auto Callback = Scene.GetSolver()->CreateAndRegisterSimCallbackObject_External<FCallback>();
		Callback->NumPTSteps = NumPTSteps;
		FReal Time = 0;
		const FReal GTDt = FixedDT * 0.25f;
		for (int32 Step = 0; Step < NumGTSteps; Step++)
		{
			//set force every external frame
			Particle2.AddForce(ConstantForce);
			FVec3 Grav(0, 0, 0);
			Scene.SetUpForFrame(&Grav, GTDt, 99999, 99999, 10, false);
			Scene.StartFrame();
			Scene.EndFrame();

			Time += GTDt;
			const FReal InterpolatedTime = Time - FixedDT * Chaos::AsyncInterpolationMultiplier;
			const FReal ExpectedVFromForce = Time;
			if (InterpolatedTime < 0)
			{
				//not enough time to interpolate so just take initial value
				EXPECT_NEAR(Particle.X()[2], ZStart, 1e-2);
				EXPECT_NEAR(Particle2.V()[2], 0, 1e-2);
			}
			else
			{
				//interpolated
				EXPECT_NEAR(Particle.X()[2], ZStart + ZVel * InterpolatedTime, 1e-2);
				EXPECT_NEAR(Particle2.V()[2], InterpolatedTime, 1e-2);
			}
		}

		EXPECT_EQ(Callback->Count, NumPTSteps);
		const FReal LastInterpolatedTime = NumGTSteps * GTDt - FixedDT * Chaos::AsyncInterpolationMultiplier;
		EXPECT_NEAR(Particle.X()[2], ZStart + ZVel * LastInterpolatedTime, 1e-2);
		EXPECT_NEAR(Particle.V()[2], ZVel, 1e-2);
	}

	GTEST_TEST(EngineInterface, PerPropertySetOnGT)
	{
		//Need to test:
		//setting transform, velocities, wake state, on external thread means we overwrite results until sim catches up
		//deleted proxy does not incorrectly update after it's deleted on gt
		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		const FReal FixedDT = 1;
		Scene.GetSolver()->EnableAsyncMode(1);	//tick 1 dt at a time

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);
		Particle.SetObjectState(EObjectStateType::Dynamic);
		const FReal ZVel = 10;
		const FReal ZStart = 100;
		Particle.SetV(FVec3(0, 0, ZVel));
		Particle.SetX(FVec3(0, 0, ZStart));
		const int32 NumGTSteps = 100;
		const FVec3 TeleportLocation(5, 5, ZStart);

		FReal Time = 0;
		const FReal GTDt = FixedDT * 0.5f;
		const int32 ChangeVelStep = 20;
		const FReal ChangeVelTime = ChangeVelStep * GTDt;
		const FReal YVelAfterChange = 10;
		const int32 TeleportStep = 10;
		const FReal TeleportTime = TeleportStep * GTDt;
		bool bHasTeleportedOnGT = false;
		bool bVelHasChanged = false;
		bool bWasPutToSleep = false;
		bool bWasWoken = false;
		const int32 SleepStep = 50;
		const int32 WakeStep = 70;
		const FReal PutToSleepTime = SleepStep * GTDt;
		const FReal WokenTime = WakeStep * GTDt;
		FReal SleepZPosition(0);

		for (int32 Step = 0; Step < NumGTSteps; Step++)
		{
			if (Step == TeleportStep)
			{
				Particle.SetX(TeleportLocation);
				bHasTeleportedOnGT = true;
			}

			if(Step == ChangeVelStep)
			{
				Particle.SetV(FVec3(0, YVelAfterChange, ZVel));
				bVelHasChanged = true;
			}

			if(Step == SleepStep)
			{
				bWasPutToSleep = true;
				Particle.SetObjectState(EObjectStateType::Sleeping);
				SleepZPosition = Particle.X()[2];	//record position when gt wants to sleep
			}

			if(Step == WakeStep)
			{
				bWasWoken = true;
				Particle.SetV(FVec3(0, YVelAfterChange, ZVel));
				Particle.SetObjectState(EObjectStateType::Dynamic);
			}

			FVec3 Grav(0, 0, 0);
			Scene.SetUpForFrame(&Grav, GTDt, 99999, 99999, 10, false);
			Scene.StartFrame();
			Scene.EndFrame();

			Time += GTDt;
			const FReal InterpolatedTime = Time - FixedDT * Chaos::AsyncInterpolationMultiplier;
			if (InterpolatedTime < 0)
			{
				//not enough time to interpolate so just take initial value
				EXPECT_NEAR(Particle.X()[2], ZStart, 1e-2);
			}
			else
			{
				//interpolated
				if(bHasTeleportedOnGT)
				{
					EXPECT_NEAR(Particle.X()[0], TeleportLocation[0], 1e-2);	//X never changes so as soon as gt teleports we should see it

					//if we haven't caught up to teleport, we just use the value set on GT for z value
					if(InterpolatedTime < TeleportTime)
					{
						EXPECT_NEAR(Particle.X()[2], TeleportLocation[2], 1e-3);
					}
					else
					{
						if(!bWasPutToSleep)
						{
							//caught up so expect normal movement to marshal back
							EXPECT_NEAR(Particle.X()[2], TeleportLocation[2] + ZVel * (InterpolatedTime - TeleportTime), 1e-2);
						}
						else if(InterpolatedTime < WokenTime)
						{
							//currently asleep so position is held constant
							EXPECT_NEAR(Particle.X()[2], SleepZPosition, 1e-2);
							if(!bWasWoken)
							{
								EXPECT_NEAR(Particle.V()[2], 0, 1e-2);
							}
							else
							{
								EXPECT_NEAR(Particle.V()[2], ZVel, 1e-2);
							}
						}
						else
						{
							//woke back up so position is moving again
							EXPECT_NEAR(Particle.X()[2], SleepZPosition + ZVel * (InterpolatedTime - WokenTime), 1e-2);
							EXPECT_NEAR(Particle.V()[2], ZVel, 1e-2);
						}
						
					}
				}
				else
				{
					EXPECT_NEAR(Particle.X()[2], ZStart + ZVel * InterpolatedTime, 1e-2);
				}

				if(bVelHasChanged)
				{
					if(!bWasPutToSleep || bWasWoken)
					{
						EXPECT_EQ(Particle.V()[1], YVelAfterChange);
					}
					else
					{
						//asleep so velocity is 0
						EXPECT_EQ(Particle.V()[1], 0);
					}
				}
				else
				{
					EXPECT_EQ(Particle.V()[1], 0);
				}

				if(bWasPutToSleep && !bWasWoken)
				{
					EXPECT_EQ(Particle.ObjectState(), EObjectStateType::Sleeping);
				}
				else
				{
					EXPECT_EQ(Particle.ObjectState(), EObjectStateType::Dynamic);
				}
			}
		}

		const FReal LastInterpolatedTime = NumGTSteps * GTDt - FixedDT * Chaos::AsyncInterpolationMultiplier;
		EXPECT_EQ(Particle.V()[2], ZVel);
		EXPECT_EQ(Particle.V()[1], YVelAfterChange);
	}

	GTEST_TEST(EngineInterface, FlushCommand)
	{
		//Need to test:
		//flushing commands works and sees state changes for both fixed dt and not
		//sim callback is not called

		bool bHitOnShutDown = false;
		{
			FChaosScene Scene(nullptr);
			Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
			Scene.GetSolver()->EnableAsyncMode(1);	//tick 1 dt at a time

			FActorCreationParams Params;
			Params.Scene = &Scene;

			FPhysicsActorHandle Proxy = nullptr;

			FChaosEngineInterface::CreateActor(Params, Proxy);
			auto& Particle = Proxy->GetGameThreadAPI();
			{
				auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
				Particle.SetGeometry(MoveTemp(Sphere));
			}

			TArray<FPhysicsActorHandle> Proxys = { Proxy };
			Scene.AddActorsToScene_AssumesLocked(Proxys);
			Particle.SetX(FVec3(0, 0, 3));

			Scene.GetSolver()->EnqueueCommandImmediate([Proxy]()
				{
					//sees change immediately
					EXPECT_EQ(Proxy->GetPhysicsThreadAPI()->X()[2], 3);
				});

			struct FCallback : public TSimCallbackObject<>
			{
				virtual void OnPreSimulate_Internal() override
				{
					EXPECT_FALSE(true);	//this should never hit
				}
			};

			auto Callback = Scene.GetSolver()->CreateAndRegisterSimCallbackObject_External<FCallback>();

			FVec3 Grav(0, 0, 0);
			Scene.SetUpForFrame(&Grav, 0, 99999, 99999, 10, false);	//flush with dt 0
			Scene.StartFrame();
			Scene.EndFrame();

			Scene.GetSolver()->EnqueueCommandImmediate([&bHitOnShutDown]()
				{
					//command enqueued and then solver shuts down, so flush must happen
					bHitOnShutDown = true;
				});
		}

		EXPECT_TRUE(bHitOnShutDown);
	}

	GTEST_TEST(EngineInterface, SimSubstep)
	{
		//Need to test:
		//forces and torques are extrapolated (i.e. held constant for sub-steps)
		//kinematic targets are interpolated over the sub-step
		//identical inputs are given to sub-steps

		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		const FReal FixedDT = 1;
		Scene.GetSolver()->EnableAsyncMode(FixedDT);	//tick 1 dt at a time

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);
		Particle.SetObjectState(EObjectStateType::Dynamic);
		Particle.SetGravityEnabled(true);

		struct FDummyInput : FSimCallbackInput
		{
			int32 ExternalFrame;
			void Reset() {}
		};

		struct FCallback : public TSimCallbackObject<FDummyInput>
		{
			virtual void OnPreSimulate_Internal() override
			{
				EXPECT_EQ(GetConsumerInput_Internal()->ExternalFrame, ExpectedFrame);
				EXPECT_NEAR(GetSimTime_Internal(), InternalSteps * GetDeltaTime_Internal(), 1e-2);	//sim start is changing per sub-step
				++InternalSteps;
			}

			int32 ExpectedFrame;
			int32 InternalSteps = 0;
		};

		auto Callback = Scene.GetSolver()->CreateAndRegisterSimCallbackObject_External<FCallback>();

		FReal Time = 0;
		const FReal GTDt = FixedDT * 4;
		for (int32 Step = 0; Step < 10; Step++)
		{
			Callback->ExpectedFrame = Step;
			Callback->GetProducerInputData_External()->ExternalFrame = Step;	//make sure input matches for all sub-steps

			//set force every external frame
			Particle.AddForce(FVec3(0, 0, 1 * Particle.M()));	//should counteract gravity
			FVec3 Grav(0, 0, -1);
			Scene.SetUpForFrame(&Grav, GTDt, 99999, 99999, 10, false);
			Scene.StartFrame();
			Scene.EndFrame();

			Time += GTDt;

			//should have no movement because forces cancel out
			EXPECT_NEAR(Particle.X()[2], 0, 1e-2);
			EXPECT_NEAR(Particle.V()[2], 0, 1e-2);
		}
	}

	GTEST_TEST(EngineInterface, SimDestroyedProxy)
	{
		//Need to test:
		//destroyed proxy still valid in callback, but Proxy is nulled out
		//valid for multiple sub-steps

		FChaosScene Scene(nullptr);
		Scene.GetSolver()->SetThreadingMode_External(EThreadingModeTemp::SingleThread);
		const FReal FixedDT = 1;
		Scene.GetSolver()->EnableAsyncMode(FixedDT);	//tick 1 dt at a time

		FActorCreationParams Params;
		Params.Scene = &Scene;

		FPhysicsActorHandle Proxy = nullptr;

		FChaosEngineInterface::CreateActor(Params, Proxy);
		auto& Particle = Proxy->GetGameThreadAPI();
		{
			auto Sphere = MakeUnique<TSphere<FReal, 3>>(FVec3(0), 3);
			Particle.SetGeometry(MoveTemp(Sphere));
		}

		TArray<FPhysicsActorHandle> Proxys = { Proxy };
		Scene.AddActorsToScene_AssumesLocked(Proxys);

		struct FDummyInput : FSimCallbackInput
		{
			FSingleParticlePhysicsProxy* Proxy;
			void Reset() {}
		};

		struct FCallback : public TSimCallbackObject<FDummyInput>
		{
			virtual void OnPreSimulate_Internal() override
			{
				EXPECT_EQ(GetConsumerInput_Internal()->Proxy->GetHandle_LowLevel(), nullptr);
			}
		};

		auto Callback = Scene.GetSolver()->CreateAndRegisterSimCallbackObject_External<FCallback>();

		Callback->GetProducerInputData_External()->Proxy = Proxy;
		Scene.GetSolver()->UnregisterObject(Proxy);

		FVec3 Grav(0, 0, -1);
		Scene.SetUpForFrame(&Grav, FixedDT * 3, 99999, 99999, 10, false);
		Scene.StartFrame();
		Scene.EndFrame();
	}
	
	GTEST_TEST(EngineInterface, OverlapOffsetActor)
	{
		FChaosScene Scene(nullptr);

		FActorCreationParams Params;
		Params.Scene = &Scene;
		Params.bSimulatePhysics = false;
		Params.bStatic = true;
		Params.InitialTM = FTransform::Identity;
		Params.Scene = &Scene;

		FPhysicsActorHandle StaticCube = nullptr;

		FChaosEngineInterface::CreateActor(Params, StaticCube);
		ASSERT_NE(StaticCube, nullptr);

		// Add geometry, placing a box at the origin
		constexpr FReal BoxSize = static_cast<FReal>(50.0);
		const FVec3 HalfBoxExtent{ BoxSize };

		// We require a union here, although the second geometry isn't used we need the particle to
		// have more than one shape in its shapes array otherwise the query acceleration will treat
		// it as a special case and skip bounds checking during the overlap
		TArray<TUniquePtr<FImplicitObject>> Geoms;
		Geoms.Emplace(MakeUnique<TBox<FReal, 3>>(-HalfBoxExtent, HalfBoxExtent));
		Geoms.Emplace(MakeUnique<TBox<FReal, 3>>(-HalfBoxExtent, HalfBoxExtent));

		auto& Particle = StaticCube->GetGameThreadAPI();
		{
			TUniquePtr<FImplicitObjectUnion> GeomUnion = MakeUnique<FImplicitObjectUnion>(MoveTemp(Geoms));
			Particle.SetGeometry(MoveTemp(GeomUnion));
		}

		TArray<FPhysicsActorHandle> Particles{ StaticCube };
		Scene.AddActorsToScene_AssumesLocked(Particles);

		FChaosSQAccelerator SQ{ *Scene.GetSpacialAcceleration() };
		FSQHitBuffer<ChaosInterface::FOverlapHit> HitBuffer;
		FOverlapAllQueryCallback QueryCallback;

		// Here we query from a position under the box, but using a shape that has an offset. This tests
		// a failure case that was previously present where the query system assumed that the QueryTM
		// was inside the geometry being used to query.
		const FTransform QueryTM{ FVec3{0.0f, 0.0f, -110.0f} };
		constexpr FReal SphereRadius = static_cast<FReal>(50.0);
		SQ.Overlap(TSphere<FReal, 3>(FVec3(0.0f, 0.0f, 100.0f), SphereRadius), QueryTM, HitBuffer, FChaosQueryFilterData(), QueryCallback, FQueryDebugParams());

		EXPECT_TRUE(HitBuffer.HasBlockingHit());
	}
}
