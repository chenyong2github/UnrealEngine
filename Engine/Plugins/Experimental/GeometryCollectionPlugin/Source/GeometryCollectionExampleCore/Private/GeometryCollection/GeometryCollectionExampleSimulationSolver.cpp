// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/ErrorReporter.h"
#include "Chaos/Defines.h"
#include "ChaosSolversModule.h"
#include "PhysicsSolver.h"
#include "EventsData.h"

#define SMALL_THRESHOLD 1e-4


// #TODO Lots of duplication in here, anyone making solver or object changes
// has to go and fix up so many callsites here and they're all pretty much
// Identical. The similar code should be pulled out

namespace GeometryCollectionExample
{

	template<class T>
	void Solver_AdvanceNoObjects()
	{
		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);
	}
	template void Solver_AdvanceNoObjects<float>();


	template<class T>
	void Solver_AdvanceDisabledObjects()
	{
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

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);

		FinalizeSolver(*Solver);

		// never touched
		TManagedArray<FTransform>& RestTransform = RestCollection->Transform;
		EXPECT_LT(FMath::Abs(RestTransform[0].GetTranslation().Z), SMALL_THRESHOLD);

		// simulated
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		EXPECT_EQ(Transform.Num(), 1);
		EXPECT_LT(FMath::Abs(Transform[0].GetTranslation().Z), SMALL_THRESHOLD);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
	}
	template void Solver_AdvanceDisabledObjects<float>();

	template<class T>
	void Solver_AdvanceDisabledClusteredObjects()
	{
		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, -10, 10)), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 10, 10)), FVector(1.0)));
		EXPECT_EQ(RestCollection->Transform.Num(), 2);


		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		EXPECT_EQ(RestCollection->Transform.Num(), 3);
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


		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float StartingRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size(), CurrentRigidDistance = 0.f;

		for (int Frame = 0; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			CurrentRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size();
		}

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

	}
	template void Solver_AdvanceDisabledClusteredObjects<float>();


	template<class T>
	void Solver_ValidateReverseMapping()
	{
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial = MakeUnique<Chaos::FChaosPhysicsMaterial>();
		InitMaterialToZero(PhysicalMaterial);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		TArray<TSharedPtr<FGeometryCollection> > RestArray;
		TArray<TSharedPtr<FGeometryDynamicCollection> > DynamicArray;

		for (int32 i = 0; i < 10; i++)
		{
			TSharedPtr<FGeometryCollection> RestCollection = nullptr;
			TSharedPtr<FGeometryDynamicCollection> DynamicCollection = nullptr;

			InitCollectionsParameters InitParams = { FTransform::Identity, FVector(1.0), nullptr, (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic };
			InitCollections(PhysicalMaterial, RestCollection, DynamicCollection, InitParams);

			FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection);

#if CHAOS_PARTICLEHANDLE_TODO
			Solver->RegisterObject(PhysObject);
#endif
			PhysObject->ActivateBodies();

			RestArray.Add(RestCollection);
			DynamicArray.Add(DynamicCollection);
		}


		Solver->AdvanceSolverBy(1 / 24.);

#if TODO_REIMPLEMENT_PHYSICS_PROXY_REVERSE_MAPPING
		const Chaos::TArrayCollectionArray<PhysicsProxyWrapper> & SolverMapping = Solver->GetPhysicsProxyReverseMapping();
		const Chaos::TArrayCollectionArray<int32> & ParticleMapping = Solver->GetParticleIndexReverseMapping();

		EXPECT_EQ(SolverMapping.Num(), 11);
		EXPECT_EQ(ParticleMapping.Num(), 11);

		EXPECT_EQ(ParticleMapping[0], INDEX_NONE);
		EXPECT_EQ(ParticleMapping[1], 0);

		EXPECT_EQ(SolverMapping[0].PhysicsProxy, nullptr);
		EXPECT_EQ(SolverMapping[0].Type, EPhysicsProxyType::NoneType);

		EXPECT_EQ(SolverMapping[5].PhysicsProxy != nullptr);
		EXPECT_EQ(SolverMapping[5].Type, EPhysicsProxyType::GeometryCollectionType);

		const TManagedArray<int32> & RigidBodyID = static_cast<FGeometryCollectionPhysicsProxy*>(SolverMapping[5].PhysicsProxy)->
			GetGeometryDynamicCollection_PhysicsThread()->GetAttribute<int32>("RigidBodyID", FGeometryCollection::TransformGroup);
		EXPECT_EQ(RigidBodyID.Num(), 1);
		EXPECT_EQ(RigidBodyID[0], 5);
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

	}
	template void Solver_ValidateReverseMapping<float>();

	template<class T>
	void CommonInit(int32 NumObjects, bool UseClusters, Chaos::FPBDRigidsSolver** SolverInOut, TUniquePtr<Chaos::FChaosPhysicsMaterial>& PhysicalMaterial, TArray<TSharedPtr<FGeometryCollection>>& RestArray, TArray<TSharedPtr<FGeometryDynamicCollection>>& DynamicArray)
	{
		PhysicalMaterial = MakeUnique<Chaos::FChaosPhysicsMaterial>();
		InitMaterialToZero(PhysicalMaterial);

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

			auto CustomFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
			{
				InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			};

			FGeometryCollectionPhysicsProxy* PhysObject = RigidBodySetup(PhysicalMaterial, RestCollection, DynamicCollection, CustomFunc);
			PhysObject->SetCollisionParticlesPerObjectFraction(1.0);

#if CHAOS_PARTICLEHANDLE_TODO
			Solver->RegisterObject(PhysObject);
#endif
			Solver->SetHasFloor(true);
			Solver->SetEnabled(true);
			PhysObject->ActivateBodies();

			RestArray.Add(RestCollection);
			DynamicArray.Add(DynamicCollection);
		}

		Solver->AdvanceSolverBy(1 / 24.);
	}

	class EventHarvester
	{
	public:
		EventHarvester(Chaos::FPBDRigidsSolver* Solver)
		{
			Solver->GetEventManager()->RegisterHandler<Chaos::FCollisionEventData>(Chaos::EEventType::Collision, this, &EventHarvester::HandleCollisionEvents);
			Solver->GetEventManager()->RegisterHandler<Chaos::FBreakingEventData>(Chaos::EEventType::Breaking, this, &EventHarvester::HandleBreakingEvents);
		}

		void HandleCollisionEvents(const Chaos::FCollisionEventData& Events)
		{
			CollisionEventData = Events;
		}

		void HandleBreakingEvents(const Chaos::FBreakingEventData& Events)
		{
			BreakingEventData = Events;
		}

		Chaos::FCollisionEventData CollisionEventData;
		Chaos::FBreakingEventData BreakingEventData;
	};

	template<class T>
	void Solver_CollisionEventFilter()
	{
		float TestMassThreshold = 6.0f;

		Chaos::FPBDRigidsSolver* Solver;
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial;
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
		EventHarvester Events(Solver);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		// modify mass for filter test
		FParticlesType& Particles = (FParticlesType&)Solver->GetRigidParticles();
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

			const auto& AllCollisionsArray = Events.CollisionEventData.CollisionData.AllCollisionsArray;
			Impact = (AllCollisionsArray.Num() > 0);

			if (Impact)
			{
				// any objects with a mass of less than 6 are removed from returned collision data
				EXPECT_EQ(AllCollisionsArray.Num(), 4);

				for (const auto& Collision : AllCollisionsArray)
				{ 
					EXPECT_GE(Particles.M(Collision.ParticleIndex), TestMassThreshold);
					EXPECT_GE(Collision.Mass1, TestMassThreshold);
					EXPECT_LT(Collision.Velocity1.Z, 0.0f);
					EXPECT_EQ(Collision.Mass2, 0.0f);
					EXPECT_EQ(Collision.Velocity2.Z, 0.0f);
				}
			}
		} while (!Impact);

#endif
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

	}
	template void Solver_CollisionEventFilter<float>();

	template<class T>
	void Solver_BreakingEventFilter()
	{
		float TestMass = 6.0f;

		Chaos::FPBDRigidsSolver* Solver;
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial;
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

		EventHarvester Events(Solver);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		// modify mass for filter test
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
		Particles.M(1) = TestMass + 1.0f;
		Particles.M(2) = TestMass - 1.0f;
		Particles.M(3) = TestMass - 2.0f;
		Particles.M(4) = TestMass + 2.0f;

		bool Impact = false;
		do
		{
			// Events data on physics thread is appended until the game thread has had a chance to Tick & read it
			Solver->SyncEvents_GameThread();
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			const auto& AllBreakingsArray = Events.BreakingEventData.BreakingData.AllBreakingsArray;
			Impact = (AllBreakingsArray.Num() > 0);

			if (Impact)
			{
				EXPECT_EQ(Particles.Disabled(0), false); // ground
				EXPECT_EQ(Particles.Disabled(1), false); // piece1 active 6 mass
				EXPECT_EQ(Particles.Disabled(2), false); // piece2 active 0.5 mass
				EXPECT_EQ(Particles.Disabled(3), false); // piece3 active 0.5 mass
				EXPECT_EQ(Particles.Disabled(4), false); // cluster active 7 mass
				EXPECT_EQ(Particles.Disabled(5), true); // cluster

				// breaking data
				EXPECT_EQ(AllBreakingsArray.Num(), 2); // 2 pieces filtered out of 4

				EXPECT_EQ(AllBreakingsArray[0].ParticleIndex, 4);
				EXPECT_EQ(AllBreakingsArray[0].Mass, TestMass + 2.0f);
				EXPECT_EQ(AllBreakingsArray[1].ParticleIndex, 1);
				EXPECT_EQ(AllBreakingsArray[1].Mass, TestMass + 1.0f);

			}
		} while (!Impact);

#endif
		FChaosSolversModule::GetModule()->DestroySolver(Solver);
	}
	template void Solver_BreakingEventFilter<float>();
}



