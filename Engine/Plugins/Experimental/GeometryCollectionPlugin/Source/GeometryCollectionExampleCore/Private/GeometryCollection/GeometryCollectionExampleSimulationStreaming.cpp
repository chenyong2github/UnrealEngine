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
	bool RigidBodies_Streaming_StartSolverEmpty(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
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

		R.ExpectTrue(Particles.Size() == 9);
		for (uint32 i = 0; i < Particles.Size() - 1; i++)
			R.ExpectTrue(Particles.X(i).Z < Particles.X(i + 1).Z);

		// cleanup
		for (auto Obj : Collections)
			delete Obj;
#endif
		delete Solver;
#endif
		return !R.HasError();
	}
	template bool RigidBodies_Streaming_StartSolverEmpty<float>(ExampleResponse&& R);


	template<class T>
	bool RigidBodies_Streaming_BulkInitialization(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
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

		R.ExpectTrue(Particles.Size() == 9);
		for (uint32 i = 0; i < Particles.Size() - 1; i++)
			R.ExpectTrue( FMath::Abs(Particles.X(i).Z - Particles.X(i + 1).Z)<KINDA_SMALL_NUMBER );


		// cleanup
		for (auto Obj : Collections)
			delete Obj;
#endif
		delete Solver;
#endif
		return !R.HasError();
	}
	template bool RigidBodies_Streaming_BulkInitialization<float>(ExampleResponse&& R); 



	template<class T>
	bool RigidBodies_Streaming_DeferedClusteringInitialization(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
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
		R.ExpectTrue(Particles.Size() == 9);
		for (uint32 i = 0; i < Particles.Size(); i++)
		{
			R.ExpectTrue(Particles.Disabled(i) == true);
		}

		for (auto Obj : Collections)
			Obj->PhysicsProxy->ActivateBodies();

		// all particles should be enabled
		R.ExpectTrue(Particles.Size() == 9);
		for (uint32 i = 0; i < Particles.Size(); i++)
		{
			R.ExpectTrue(Particles.Disabled(i) == false);
		}

		for (int32 Frame = 1; Frame < 100; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
		}

		// new cluster parent should be falling
		R.ExpectTrue(Particles.Size() == 10);
		for (uint32 i = 0; i < Particles.Size()-1; i++)
		{
			R.ExpectTrue(Particles.Disabled(i) == true);
		}
		R.ExpectTrue(Particles.Disabled(Particles.Size()-1) == false);
		R.ExpectTrue(Particles.X(Particles.Size() - 1).Z < -1.f);

		// cleanup
		for (auto Obj : Collections)
			delete Obj;
#endif
		delete Solver;
#endif
		return !R.HasError();
	}
	template bool RigidBodies_Streaming_DeferedClusteringInitialization<float>(ExampleResponse&& R);


}

