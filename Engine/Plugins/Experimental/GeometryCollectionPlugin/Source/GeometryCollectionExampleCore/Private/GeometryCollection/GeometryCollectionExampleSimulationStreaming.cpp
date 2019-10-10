// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleSimulationStreaming.h"
#include "GeometryCollection/GeometryCollectionExampleUtility.h"
#include "GeometryCollection/GeometryCollectionExampleSimulationObject.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"
#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/ErrorReporter.h"
#include "ChaosSolversModule.h"
#include "PBDRigidsSolver.h"

#define SMALL_THRESHOLD 1e-4

namespace GeometryCollectionExample
{
	template<class T>
	void RigidBodies_Streaming_StartSolverEmpty()
	{
		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();

		TArray< SimulationObjects<T>* > Collections;
		for (int32 Frame = 1; Frame < 1000; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			if (Frame % 100 == 0)
			{
				Collections.Add(new SimulationObjects<T>());
				Solver->RegisterObject(Collections.Last()->PhysicsProxy.Get());
				Collections.Last()->PhysicsProxy.Get()->Initialize();
				Collections.Last()->PhysicsProxy.Get()->ActivateBodies();
			}
		}

		EXPECT_EQ(Particles.Size(), 9);
		for (uint32 i = 0; i < Particles.Size() - 1; i++)
			EXPECT_LT(Particles.X(i).Z, Particles.X(i + 1).Z);

		// cleanup
		for (auto Obj : Collections)
			delete Obj;
#endif
		delete Solver;
	}
	template void RigidBodies_Streaming_StartSolverEmpty<float>();


	template<class T>
	void RigidBodies_Streaming_BulkInitialization()
	{
		typename SimulationObjects<T>::FParameters P;
		P.CollisionGroup = -1;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();

		TArray< SimulationObjects<T>* > Collections;
		for (int32 Frame = 1; Frame < 1000; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			if (Frame % 100 == 0)
			{
				Collections.Add(new SimulationObjects<T>(P));
				Solver->RegisterObject(Collections.Last()->PhysicsProxy.Get());
				Collections.Last()->PhysicsProxy.Get()->Initialize();
			}
		}
		for (auto Obj : Collections)
			Obj->PhysicsProxy->ActivateBodies();

		for (int32 Frame = 1; Frame < 100; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
		}

		EXPECT_EQ(Particles.Size(), 9);
		for (uint32 i = 0; i < Particles.Size() - 1; i++)
			EXPECT_LT( FMath::Abs(Particles.X(i).Z - Particles.X(i + 1).Z), KINDA_SMALL_NUMBER );


		// cleanup
		for (auto Obj : Collections)
			delete Obj;
#endif
		delete Solver;
	}
	template void RigidBodies_Streaming_BulkInitialization<float>(); 



	template<class T>
	void RigidBodies_Streaming_DeferedClusteringInitialization()
	{
		typename SimulationObjects<T>::FParameters P;
		P.CollisionGroup = -1;
		P.EnableClustering = true;
		P.ClusterGroupIndex = 1;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();

		TArray< SimulationObjects<T>* > Collections;
		for (int32 Frame = 1; Frame < 1000; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			if (Frame % 100 == 0)
			{
				Collections.Add(new SimulationObjects<T>(P, GeometryCollection::MakeCubeElement(FTransform(FQuat(ForceInit), FVector(Frame)), FVector(1.0))));
				Solver->RegisterObject(Collections.Last()->PhysicsProxy.Get());
				Collections.Last()->PhysicsProxy.Get()->Initialize();
			}
		}

		// all particles should be disabled
		EXPECT_EQ(Particles.Size(), 9);
		for (uint32 i = 0; i < Particles.Size(); i++)
		{
			EXPECT_EQ(Particles.Disabled(i), true);
		}

		for (auto Obj : Collections)
			Obj->PhysicsProxy->ActivateBodies();

		// all particles should be enabled
		EXPECT_EQ(Particles.Size(), 9);
		for (uint32 i = 0; i < Particles.Size(); i++)
		{
			EXPECT_EQ(Particles.Disabled(i), false);
		}

		for (int32 Frame = 1; Frame < 100; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
		}

		// new cluster parent should be falling
		EXPECT_EQ(Particles.Size(), 10);
		for (uint32 i = 0; i < Particles.Size()-1; i++)
		{
			EXPECT_EQ(Particles.Disabled(i), true);
		}
		EXPECT_EQ(Particles.Disabled(Particles.Size()-1), false);
		EXPECT_LT(Particles.X(Particles.Size() - 1).Z, -1.f);

		// cleanup
		for (auto Obj : Collections)
			delete Obj;
#endif
		delete Solver;
	}
	template void RigidBodies_Streaming_DeferedClusteringInitialization<float>();


}

