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


namespace ChaosTest {

    using namespace Chaos;
	
	GTEST_TEST(EngineInterface, CreateActor)
	{
		FChaosScene Scene(nullptr);
		
		FActorCreationParams Params;
		Params.Scene = &Scene;

		TGeometryParticle<FReal,3>* Particle = nullptr;

		FChaosEngineInterface::CreateActor(Params,Particle);
		EXPECT_NE(Particle,nullptr);
		
		FChaosEngineInterface::ReleaseActor(Particle,&Scene);
		EXPECT_EQ(Particle,nullptr);
		
	}
}
