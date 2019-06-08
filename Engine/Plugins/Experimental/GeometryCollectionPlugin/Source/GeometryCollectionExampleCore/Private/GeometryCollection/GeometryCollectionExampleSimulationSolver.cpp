// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleSimulationSolver.h"
#include "GeometryCollection/GeometryCollectionExampleUtility.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"


#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"
#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "SolverObjects/GeometryCollectionPhysicsObject.h"
#include "SolverObjects/SolverObjects.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Defines.h"
#include "ChaosSolversModule.h"

#define SMALL_THRESHOLD 1e-4


// #TODO Lots of duplication in here, anyone making solver or object changes
// has to go and fix up so many callsites here and they're all pretty much
// Identical. The similar code should be pulled out

namespace GeometryCollectionExample
{

	template<class T>
	bool Solver_AdvanceNoObjects(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
#endif

		return !R.HasError();
	}
	template bool Solver_AdvanceNoObjects<float>(ExampleResponse&& R);


	template<class T>
	bool Solver_AdvanceDisabledObjects(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		auto InitFunc = [&RestCollection, &DynamicCollection](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
			InParams.Simulating = false;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter,*RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsObject* PhysObject = new FGeometryCollectionPhysicsObject(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);

		FinalizeSolver(*Solver);

		// never touched
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		R.ExpectTrue(FMath::Abs(RestTransform[0].GetTranslation().Z) < SMALL_THRESHOLD);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		R.ExpectTrue(Transform.Num() == 1);
		R.ExpectTrue(FMath::Abs(Transform[0].GetTranslation().Z) < SMALL_THRESHOLD);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

#endif
		return !R.HasError();
	}
	template bool Solver_AdvanceDisabledObjects<float>(ExampleResponse&& R);

	template<class T>
	bool Solver_AdvanceDisabledClusteredObjects(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, -10, 10)), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 10, 10)), FVector(1.0)));
		R.ExpectTrue(RestCollection->Transform.Num() == 2);


		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		R.ExpectTrue(RestCollection->Transform.Num() == 3);
		RestCollection->Transform[2] = FTransform(FQuat::MakeFromEuler(FVector(90.f, 0, 0.)), FVector(0, 0, 40));

		//GeometryCollectionAlgo::PrintParentHierarchy(RestCollection.Get());

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		auto InitFunc = [&RestCollection, &DynamicCollection](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.DamageThreshold = { 1000.f };
			InParams.Simulating = false;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};


		FGeometryCollectionPhysicsObject* PhysObject = new FGeometryCollectionPhysicsObject(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->RegisterObject(PhysObject);
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float StartingRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size(), CurrentRigidDistance = 0.f;

		for (int Frame = 0; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
			CurrentRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size();
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

#endif


		return !R.HasError();
	}
	template bool Solver_AdvanceDisabledClusteredObjects<float>(ExampleResponse&& R);


	template<class T>
	bool Solver_ValidateReverseMapping(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		TArray<TSharedPtr<FGeometryCollection> > RestArray;
		TArray<TSharedPtr<FGeometryDynamicCollection> > DynamicArray;

		for (int32 i = 0; i < 10; i++)
		{
			TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0));
			TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

			auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
			{
				InParams.RestCollection = RestCollection.Get();
				InParams.DynamicCollection = DynamicCollection.Get();
				InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
				InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
				InParams.Simulating = true;
				Chaos::FErrorReporter ErrorReporter;
				BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
			};

			FGeometryCollectionPhysicsObject* PhysObject = new FGeometryCollectionPhysicsObject(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
			PhysObject->Initialize();

			Solver->RegisterObject(PhysObject);
			PhysObject->ActivateBodies();

			RestArray.Add(RestCollection);
			DynamicArray.Add(DynamicCollection);
		}


		Solver->AdvanceSolverBy(1 / 24.);

		const Chaos::TArrayCollectionArray<SolverObjectWrapper> & SolverMapping = Solver->GetSolverObjectReverseMapping();
		const Chaos::TArrayCollectionArray<int32> & ParticleMapping = Solver->GetParticleIndexReverseMapping();

		R.ExpectTrue(SolverMapping.Num() == 11);
		R.ExpectTrue(ParticleMapping.Num() == 11);

		R.ExpectTrue(ParticleMapping[0] == INDEX_NONE);
		R.ExpectTrue(ParticleMapping[1] == 0);

		R.ExpectTrue(SolverMapping[0].SolverObject == nullptr);
		R.ExpectTrue(SolverMapping[0].Type == ESolverObjectType::NoneType);

		R.ExpectTrue(SolverMapping[5].SolverObject != nullptr);
		R.ExpectTrue(SolverMapping[5].Type == ESolverObjectType::GeometryCollectionType);

		const TManagedArray<int32> & RigidBodyID = static_cast<FGeometryCollectionPhysicsObject*>(SolverMapping[5].SolverObject)->
			GetGeometryDynamicCollection_PhysicsThread()->GetAttribute<int32>("RigidBodyID", FGeometryCollection::TransformGroup);
		R.ExpectTrue(RigidBodyID.Num() == 1);
		R.ExpectTrue(RigidBodyID[0] == 5);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

#endif
		return !R.HasError();
	}
	template bool Solver_ValidateReverseMapping<float>(ExampleResponse&& R);

#if INCLUDE_CHAOS
	template<class T>
	void CommonInit(int32 NumObjects, bool UseClusters, Chaos::FPBDRigidsSolver** SolverInOut, TUniquePtr<Chaos::TChaosPhysicsMaterial<T>>& PhysicalMaterial, TArray<TSharedPtr<FGeometryCollection>>& RestArray, TArray<TSharedPtr<FGeometryDynamicCollection>>& DynamicArray)
	{
		PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;

		*SolverInOut = FChaosSolversModule::GetModule()->CreateSolver(true);
		Chaos::FPBDRigidsSolver* Solver = *SolverInOut;
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);

		for (int32 i = 0; i < NumObjects; i++)
		{

			TSharedPtr<FGeometryCollection> RestCollection;

			if (UseClusters)
			{
				RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, -10, 10)), FVector(1.0));
				RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 10, 10)), FVector(1.0)));
				RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 0, 0)), FVector(1.0)));
				RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 0, 10)), FVector(1.0)));
				FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
				RestCollection->Transform[4] = FTransform(FQuat::MakeFromEuler(FVector(90.f, 0, 0.)), FVector(0, 0, 40));
			}
			else
			{
				RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(i * 3, 0, 5)), FVector(1.0));
			}
			
			TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

			auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
			{
				InParams.RestCollection = RestCollection.Get();
				InParams.DynamicCollection = DynamicCollection.Get();
				InParams.PhysicalMaterial = MakeSerializable(PhysicalMaterial);
				InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;
				InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
				InParams.DamageThreshold = { 0.1f };
				InParams.Simulating = true;
				Chaos::FErrorReporter ErrorReporter;
				BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
			};

			FGeometryCollectionPhysicsObject* PhysObject = new FGeometryCollectionPhysicsObject(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
			PhysObject->Initialize();
			PhysObject->SetCollisionParticlesPerObjectFraction(1.0);

			Solver->RegisterObject(PhysObject);
			Solver->SetHasFloor(true);
			Solver->SetEnabled(true);
			PhysObject->ActivateBodies();

			RestArray.Add(RestCollection);
			DynamicArray.Add(DynamicCollection);
		}

		Solver->AdvanceSolverBy(1 / 24.);
	}
#endif // INCLUDE_CHAOS

	template<class T>
	bool Solver_CollisionEventFilter(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS

		float TestMassThreshold = 6.0f;

		Chaos::FPBDRigidsSolver* Solver;
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial;
		TArray<TSharedPtr<FGeometryCollection> > RestArray;
		TArray<TSharedPtr<FGeometryDynamicCollection> > DynamicArray;

		CommonInit<T>(10, false, &Solver, PhysicalMaterial, RestArray, DynamicArray);

		// setup collision filter
		FSolverCollisionFilterSettings CollisionFilterSettings;
		CollisionFilterSettings.FilterEnabled = true;
		CollisionFilterSettings.MinImpulse = 0;
		CollisionFilterSettings.MinMass = TestMassThreshold;
		CollisionFilterSettings.MinSpeed = 0;

		Solver->SetGenerateCollisionData(true);
		Solver->SetCollisionFilterSettings(CollisionFilterSettings);

		// modify mass for filter test
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
		for (int32 i = 0; i < 10; i++)
		{
			Particles.M(i) = i;
		}

		bool Impact = false;
		do
		{
			// Events data on physics thread is appended until the game thread has had a chance to Tick & read it
			Solver->SyncEvents_GameThread();
			Solver->AdvanceSolverBy(1 / 24.);

			Chaos::FPBDRigidsSolver::FScopedGetEventsData SopedAccess = Solver->ScopedGetEventsData();
			const Chaos::FPBDRigidsSolver::FAllCollisionDataMaps& CollisionMaps = SopedAccess.GetAllCollisions_Maps();

			Impact = (CollisionMaps.AllCollisionData != nullptr);

			if (Impact)
			{
				// any objects with a mass of less than 6 are removed from returned collision data
				const auto& AllCollisionsArray = CollisionMaps.AllCollisionData->AllCollisionsArray;
				R.ExpectTrue(AllCollisionsArray.Num() == 4);

				for (const auto& Collision : AllCollisionsArray)
				{ 
					R.ExpectTrue(Particles.M(Collision.ParticleIndex) >= TestMassThreshold);
					R.ExpectTrue(Collision.Mass1 >= TestMassThreshold);
					R.ExpectTrue(Collision.Velocity1.Z < 0.0f);
					R.ExpectTrue(Collision.Mass2 == 0.0f);
					R.ExpectTrue(Collision.Velocity2.Z == 0.0f);
				}
			}

		} while (!Impact);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

#endif
		return !R.HasError();
	}
	template bool Solver_CollisionEventFilter<float>(ExampleResponse&& R);


	template<class T>
	bool Solver_BreakingEventFilter(ExampleResponse&& R)
	{
#if INCLUDE_CHAOS

		float TestMass = 6.0f;

		Chaos::FPBDRigidsSolver* Solver;
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial;
		TArray<TSharedPtr<FGeometryCollection> > RestArray;
		TArray<TSharedPtr<FGeometryDynamicCollection> > DynamicArray;

		CommonInit<T>(1, true, &Solver, PhysicalMaterial, RestArray, DynamicArray);

		// setup breaking filter
		FSolverBreakingFilterSettings BreakingFilterSettings;
		BreakingFilterSettings.FilterEnabled = true;
		BreakingFilterSettings.MinMass = TestMass;
		BreakingFilterSettings.MinSpeed = 0;
		BreakingFilterSettings.MinVolume = 0;

		Solver->SetGenerateBreakingData(true);
		Solver->SetBreakingFilterSettings(BreakingFilterSettings);

		// modify mass for filter test
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
		Particles.M(1) = TestMass+1.0f;
		Particles.M(2) = TestMass - 1.0f;
		Particles.M(3) = TestMass - 2.0f;
		Particles.M(4) = TestMass+2.0f;


		bool Impact = false;
		do
		{
			// Events data on physics thread is appended until the game thread has had a chance to Tick & read it
			Solver->SyncEvents_GameThread();
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			Chaos::FPBDRigidsSolver::FScopedGetEventsData SopedAccess = Solver->ScopedGetEventsData();
			const Chaos::FPBDRigidsSolver::FAllBreakingDataMaps& BreakingMaps = SopedAccess.GetAllBreakings_Maps();

			Impact = (BreakingMaps.AllBreakingData != nullptr);

			if (Impact)
			{
				R.ExpectTrue(Particles.Disabled(0) == false); // ground
				R.ExpectTrue(Particles.Disabled(1) == false); // piece1 active 6 mass
				R.ExpectTrue(Particles.Disabled(2) == false); // piece2 active 0.5 mass
				R.ExpectTrue(Particles.Disabled(3) == false); // piece3 active 0.5 mass
				R.ExpectTrue(Particles.Disabled(4) == false); // cluster active 7 mass
				R.ExpectTrue(Particles.Disabled(5) == true); // cluster

				// breaking data
				const auto& AllBreakingsArray = BreakingMaps.AllBreakingData->AllBreakingsArray;
				R.ExpectTrue(AllBreakingsArray.Num() == 2); // 2 pieces filtered out of 4

				R.ExpectTrue(AllBreakingsArray[0].ParticleIndex == 4);
				R.ExpectTrue(AllBreakingsArray[0].Mass == TestMass + 2.0f);
				R.ExpectTrue(AllBreakingsArray[1].ParticleIndex == 1);
				R.ExpectTrue(AllBreakingsArray[1].Mass == TestMass + 1.0f);

			}

		} while (!Impact);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

#endif
		return !R.HasError();
	}
	template bool Solver_BreakingEventFilter<float>(ExampleResponse&& R);
}



