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

		TArray<TGeometryParticle<FReal,3>*> Particles ={Particle};
		Scene.AddActorsToScene_AssumesLocked(Particles);

		//make sure acceleration structure has new actor right away
		{
			FChaosSQAccelerator SQAccelerator(*Scene.GetSpacialAcceleration());
			ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit> HitBuffer;
			FOverlapAllQueryCallback QueryCallback;
			SQAccelerator.Overlap(TSphere<FReal,3>(FVec3(0),10),FTransform::Identity,HitBuffer,FChaosQueryFilterData(),QueryCallback,FQueryDebugParams());

			EXPECT_EQ(HitBuffer.GetNumHits(),1);
		}
		
		
		FChaosEngineInterface::ReleaseActor(Particle,&Scene);
		EXPECT_EQ(Particle,nullptr);

		//make sure acceleration structure no longer has actor right away
		{
			FChaosSQAccelerator SQAccelerator(*Scene.GetSpacialAcceleration());
			ChaosInterface::FSQHitBuffer<ChaosInterface::FOverlapHit> HitBuffer;
			FOverlapAllQueryCallback QueryCallback;
			SQAccelerator.Overlap(TSphere<FReal,3>(FVec3(0),10),FTransform::Identity,HitBuffer,FChaosQueryFilterData(),QueryCallback,FQueryDebugParams());

			EXPECT_EQ(HitBuffer.GetNumHits(),0);
		}
		
	}
}
