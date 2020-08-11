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
}
