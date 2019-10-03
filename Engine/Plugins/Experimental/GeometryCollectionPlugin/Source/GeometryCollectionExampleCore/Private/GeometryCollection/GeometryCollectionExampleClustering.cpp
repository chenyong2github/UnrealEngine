// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleClustering.h"
#include "GeometryCollection/GeometryCollectionExampleUtility.h"
#include "GeometryCollection/GeometryCollectionExampleSimulationObject.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"

#include "GeometryCollection/TransformCollection.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Field/FieldSystem.h"
#include "Field/FieldSystemNodes.h"


#include "GeometryCollection/GeometryDynamicCollection.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "Chaos/ErrorReporter.h"
#include "ChaosSolversModule.h"
#include "PhysicsSolver.h"
#include "Chaos/PBDRigidClustering.h"

#include "HAL/IConsoleManager.h"


DEFINE_LOG_CATEGORY_STATIC(GCTCL_Log, Verbose, All);

// #TODO Lots of duplication in here, anyone making solver or object changes
// has to go and fix up so many callsites here and they're all pretty much
// Identical. The similar code should be pulled out

namespace GeometryCollectionExample
{
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
	bool ClusterMapContains(const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap, int Key, TArray<int32> Elements)
	{
		if (ClusterMap.Num())
			if (ClusterMap.Contains(Key))
				if (ClusterMap[Key] != nullptr)
					if (ClusterMap[Key]->Num() == Elements.Num())
					{
						for (int32 Element : Elements)
							if (!ClusterMap[Key]->Contains(Element))
								return false;
						return true;
					}
		return false;
	}

	template<class T>
	void RigidBodies_ClusterTest_SingleLevelNonBreaking()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, -10, 10)),FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 10, 10)), FVector(1.0)));
		EXPECT_EQ(RestCollection->Transform.Num(), 2);

		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		EXPECT_EQ(RestCollection->Transform.Num(), 3);
		RestCollection->Transform[2] = FTransform(FQuat::MakeFromEuler(FVector(90.f, 0, 0.)), FVector(0, 0, 40));

		//GeometryCollectionAlgo::PrintParentHierarchy(RestCollection.Get());

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.DamageThreshold = {1000.f};
			InParams.Simulating = true;
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

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
		//const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();
		const auto & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();
		//EXPECT_TRUE(ClusterMapContains(ClusterMap, 3, { 1,2 }));
#endif


		for (int Frame = 1; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			CurrentRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size();
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			EXPECT_FALSE(Particles.Disabled(0));
			EXPECT_TRUE(Particles.Disabled(1));
			EXPECT_TRUE(Particles.Disabled(2));
			EXPECT_FALSE(Particles.Disabled(3));
#endif
			EXPECT_LT(FMath::Abs(CurrentRigidDistance - StartingRigidDistance), 1e-4);
		}

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 3, { 1,2 }));
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

	}
	template void RigidBodies_ClusterTest_SingleLevelNonBreaking<float>();


	template<class T>
	void RigidBodies_ClusterTest_DeactivateClusterParticle()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(20.f)),FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(40.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(50.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(60.f)), FVector(1.0)));

		RestCollection->AddElements(4, FGeometryCollection::TransformGroup);
		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		(RestCollection->SimulationType)[5] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[6] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[7] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[8] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 5, { 4,3 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 6, { 5,2 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 7, { 6,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 8, { 7,0 });

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic);
		

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.DamageThreshold = {50.0, 50.0, 50.0, FLT_MAX};
			InParams.Simulating = true;
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

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 9, { 1,8 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 8, { 7,2 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));


		TArray<bool> Conditions = { false,false };

		for (int Frame = 1; Frame < 4; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

			if (Frame == 2)
			{
				Solver->GetRigidClustering().DeactivateClusterParticle(9);
			}

			UE_LOG(GCTCL_Log, Verbose, TEXT("FRAME : %d"), Frame);
			for (int32 rdx = 0; rdx < (int32)Particles.Size(); rdx++)
			{
			  UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...Disabled[%d] : %d"), rdx, Particles.Disabled(rdx));
			  UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...    InvM[%d] : %f"), rdx, Particles.InvM(rdx));
			}

			if (Conditions[0] == false && Frame == 1)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == true &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == false)
				{
					Conditions[0] = true;
					EXPECT_EQ(Particles.InvM(9), 0.f); // kinematic cluster
					EXPECT_EQ(Particles.InvM(8), 0.f); // disabled child
					EXPECT_EQ(Particles.InvM(1), 0.f); // disabled child
				}
			}
			else if (Conditions[0] == true && Conditions[1] == false && Frame == 2)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == false &&
					Particles.Disabled(9) == true)
				{
					Conditions[1] = true;
					EXPECT_EQ(Particles.InvM(9), 0.f); // disabled cluster body
					EXPECT_EQ(Particles.InvM(1), 0.f); // enabled child
					EXPECT_EQ(Particles.InvM(8), 0.f); // enabled child

					EXPECT_TRUE(!ClusterMap.Contains(9));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 8, { 7,2 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));
				}
			}
		}
		for (int i = 0; i < Conditions.Num(); i++)
		{
			EXPECT_TRUE(Conditions[i]);
		}
#endif
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

		
	}
	template void RigidBodies_ClusterTest_DeactivateClusterParticle<float>();



	template<class T>
	void RigidBodies_ClusterTest_SingleLevelBreaking()
	{
		//
		// Test overview:
		// Create two 1cm cubes in a cluster arranged vertically and 20cm apart.
		// Position the cluster above the ground.
		// Wait until the cluster hits the ground.
		// Ensure that the cluster breaks and that the children have the correct states from then on.
		//
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, -10, 10)),FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 10, 10)), FVector(1.0)));
		EXPECT_EQ(RestCollection->Transform.Num(), 2);


		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		EXPECT_EQ(RestCollection->Transform.Num(), 3);
		RestCollection->Transform[2] = FTransform(FQuat::MakeFromEuler(FVector(90.f, 0, 0.)), FVector(0, 0, 40));

		//GeometryCollectionAlgo::PrintParentHierarchy(RestCollection.Get());

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.DamageThreshold = {0.1f};
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);
		PhysObject->Initialize();
		PhysObject->SetCollisionParticlesPerObjectFraction(1.0);

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float StartingRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size(), CurrentRigidDistance = 0.f;

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 3, { 1,2 }));
#endif

		// Particles array contains the following:
		// 0: Ground
		// 1: Box1 (top)
		// 2: Box2 (bottom)
		// 3: Box1+Box2 Cluster
		for (int Frame = 1; Frame < 20; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			CurrentRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size();
			if (Frame < 5)
			{
				// The two boxes are dropping to the ground as a cluster
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
				EXPECT_FALSE(Particles.Disabled(0));
				EXPECT_TRUE(Particles.Disabled(1));
				EXPECT_TRUE(Particles.Disabled(2));
				EXPECT_FALSE(Particles.Disabled(3));
#endif
				EXPECT_LT(FMath::Abs(CurrentRigidDistance - StartingRigidDistance), 1e-4);
			}
			else if (Frame == 5)
			{
				// The cluster has just hit the ground and should have broken.
				// The boxes are still separated by StartingRigidDistance (when Rewind is disabled).
				// All children should have zero velocity.
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
				EXPECT_FALSE(Particles.Disabled(0));
				EXPECT_FALSE(Particles.Disabled(1));
				EXPECT_FALSE(Particles.Disabled(2));
				EXPECT_TRUE(Particles.Disabled(3));
				EXPECT_LT(Particles.V(1).Size(), 1.e-4);
				EXPECT_LT(Particles.V(2).Size(), 1.e-4);
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
				EXPECT_EQ(ClusterMap.Num(), 0);
#endif
				EXPECT_LT(FMath::Abs(CurrentRigidDistance - StartingRigidDistance), 1e-4);
			}
			else if (Frame == 6)
			{
				// The boxes are now moving independently, but they had zero velocity 
				// last frame, so they should still be separated by StartingRigidDistance. 
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
				EXPECT_FALSE(Particles.Disabled(0));
				EXPECT_FALSE(Particles.Disabled(1));
				EXPECT_FALSE(Particles.Disabled(2));
				EXPECT_TRUE(Particles.Disabled(3));
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
				EXPECT_EQ(ClusterMap.Num(), 0);
#endif
				EXPECT_LT(FMath::Abs(CurrentRigidDistance - StartingRigidDistance), 1e-4);
			}
			else
			{
				// The boxes are now moving independently - the bottom one is on the ground and should be stopped.
				// The top one is still falling, so they should be closer together
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
				EXPECT_FALSE(Particles.Disabled(0));
				EXPECT_FALSE(Particles.Disabled(1));
				EXPECT_FALSE(Particles.Disabled(2));
				EXPECT_TRUE(Particles.Disabled(3));
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
				EXPECT_EQ(ClusterMap.Num(), 0);
#endif
			}
		}
		
		EXPECT_GT(FMath::Abs(CurrentRigidDistance - StartingRigidDistance), 1e-4);
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

	}
	template void RigidBodies_ClusterTest_SingleLevelBreaking<float>();


	template<class T>
	void RigidBodies_ClusterTest_NestedCluster()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, -10, 10)),FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 10, 10)), FVector(1.0)));
		EXPECT_EQ(RestCollection->Transform.Num(), 2);

		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		EXPECT_EQ(RestCollection->Transform.Num(), 3);
		RestCollection->Transform[2] = FTransform(FQuat::MakeFromEuler(FVector(90.f, 0, 0.)), FVector(0, 0, 40));

		FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(RestCollection.Get(), 3, { 2 }, true);
		EXPECT_EQ(RestCollection->Transform.Num(), 4);
		RestCollection->Transform[3] = FTransform(FQuat::MakeFromEuler(FVector(0.f, 0, 0.)), FVector(0, 0, 10));

		//GeometryCollectionAlgo::PrintParentHierarchy(RestCollection.Get());

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.DamageThreshold = {0.1f};
			InParams.Simulating = true;
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

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 3, { 1,2 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 4, { 3, }));

		TArray<bool> Conditions = {false,false,false};

		for (int Frame = 1; Frame < 20; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

			CurrentRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size();

			if (Conditions[0]==false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == true &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == false) 
				{
					Conditions[0] = true;
				}
			}
			else if (Conditions[0]==true && Conditions[1] == false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == true &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == false &&
					Particles.Disabled(4) == true)
				{
					Conditions[1] = true;
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 3, { 1,2 }));
					EXPECT_EQ(ClusterMap.Num(), 1);
					EXPECT_TRUE(!ClusterMap.Contains(4));
				}
			}
			else if (Conditions[1] == true && Conditions[2] == false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == false &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true)
				{
					Conditions[2] = true;
					EXPECT_EQ(ClusterMap.Num(), 0);
				}
			}
		}
		for (int i = 0; i < Conditions.Num(); i++)
		{
			EXPECT_TRUE(Conditions[i]);
		}
#endif
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

		
	}
	template void RigidBodies_ClusterTest_NestedCluster<float>();


	template<class T>
	void RigidBodies_ClusterTest_NestedCluster_MultiStrain()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(20.f)),FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(40.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(50.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(60.f)), FVector(1.0)));

		RestCollection->AddElements(4, FGeometryCollection::TransformGroup);
		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		(RestCollection->SimulationType)[5] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[6] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[7] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[8] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 5, { 4,3 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 6, { 5,2 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 7, { 6,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 8, { 7,0 });

		// @todo(brice->Bill.Henderson) Why did this not work? I needed to build my own parenting and level initilization. 
		//FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		//FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(RestCollection.Get(), 4, { 0, 1 }, true);
		//FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(RestCollection.Get(), 4, { 2, 3 }, true);

		GeometryCollectionAlgo::PrintParentHierarchy(RestCollection.Get());

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());
		
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.DamageThreshold = {50.0, 50.0, 50.0, FLT_MAX};
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
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

		TArray<bool> Conditions = { false,false,false };

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();
		EXPECT_EQ(ClusterMap.Num(), 4);
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 9, { 1,8 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 8, { 7,2 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));

		for (int Frame = 1; Frame < 20; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

			CurrentRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size();

			if (Conditions[0] == false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == true &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == false)
				{
					Conditions[0] = true;
				}
			}
			else if (Conditions[0] == true && Conditions[1] == false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == false &&
					Particles.Disabled(9) == true)
				{
					Conditions[1] = true;

					EXPECT_EQ(ClusterMap.Num(), 3);
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 8, { 7,2 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));
				}
			}
			else if (Conditions[1] == true && Conditions[2] == false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == false &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == false &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == true)
				{
					Conditions[2] = true;

					EXPECT_EQ(ClusterMap.Num(), 2);
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));
				}
			}
		}
		for (int i = 0; i < Conditions.Num(); i++)
		{
			EXPECT_TRUE(Conditions[i]);
		}
#endif
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

		
	}
	template void RigidBodies_ClusterTest_NestedCluster_MultiStrain<float>();



	template<class T>
	void RigidBodies_ClusterTest_NestedCluster_Halt()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(20.f)),FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(40.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(50.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(60.f)), FVector(1.0)));

		RestCollection->AddElements(4, FGeometryCollection::TransformGroup);
		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		(RestCollection->SimulationType)[5] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[6] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[7] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		(RestCollection->SimulationType)[8] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 5, { 4,3 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 6, { 5,2 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 7, { 6,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 8, { 7,0 });

		// @todo(brice->Bill.Henderson) Why did this not work? I needed to build my own parenting and level initilization. 
		//FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		//FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(RestCollection.Get(), 4, { 0, 1 }, true);
		//FGeometryCollectionClusteringUtility::ClusterBonesUnderNewNode(RestCollection.Get(), 4, { 2, 3 }, true);

		GeometryCollectionAlgo::PrintParentHierarchy(RestCollection.Get());

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());
		
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.DamageThreshold = {50.0, 50.0, 50.0, FLT_MAX};
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
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

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		TArray<bool> Conditions = { false,false };

		for (int Frame = 0; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);


			const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();

			CurrentRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size();

			//UE_LOG(GCTCL_Log, Verbose, TEXT("FRAME : %d"), Frame);
			//for (int rdx = 0; rdx < Transform.Num(); rdx++)
			//{
			//UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ... Position[%d] : (%3.5f,%3.5f,%3.5f)"), rdx, Transform[rdx].GetTranslation().X, Transform[rdx].GetTranslation().Y, Transform[rdx].GetTranslation().Z);
			//}
			//for (int32 rdx = 0; rdx < (int32)Particles.Size(); rdx++)
			//{
			//UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...Disabled[%d] : %d"), rdx, Particles.Disabled(rdx));
			//}
			//UE_LOG(GCTCL_Log, Verbose, TEXT("StartingRigidDistance : %3.5f"), StartingRigidDistance);
			//UE_LOG(GCTCL_Log, Verbose, TEXT("DeltaRigidDistance : %3.5f"), CurrentRigidDistance - StartingRigidDistance);
			
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			if (Conditions[0] == false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == true &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == false)
				{
					Conditions[0] = true;
				}
			}
			else if (Conditions[0] == true && Conditions[1] == false)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == false &&
					Particles.Disabled(9) == true)
				{
					Conditions[1] = true;
				}
			}
#endif
		}
		for (int i = 0; i < Conditions.Num(); i++)
		{
			EXPECT_TRUE(Conditions[i]);
		}
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

		
	}
	template void RigidBodies_ClusterTest_NestedCluster_Halt<float>();

	template<class T>
	void RigidBodies_ClusterTest_KinematicAnchor()
	{
		// Test : Set one element kinematic. When the cluster breaks the elements that do not contain the kinematic
		//        rigid body should be dynamic, while the clusters that contain the kinematic body should remain 
		//        kinematic. 
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(20.f)), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(40.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(50.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(60.f)), FVector(1.0)));

		RestCollection->AddElements(4, FGeometryCollection::TransformGroup);
		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[5] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[6] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[7] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[8] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 5, { 4,3 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 6, { 5,2 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 7, { 6,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 8, { 7,0 });

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);
		DynamicCollection->GetAttribute<int32>("DynamicState", FGeometryCollection::TransformGroup)[1] = (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic;
		
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.DamageThreshold = {50.0, 50.0, 50.0, FLT_MAX};
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
#endif

#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float StartingRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size(), CurrentRigidDistance = 0.f;

		TArray<bool> Conditions = { false,false,false,false };

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();

		EXPECT_EQ(ClusterMap.Num(), 4);
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 9, { 1,8 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 8, { 7,2 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));

		for (int Frame = 1; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

			if (Frame == 2)
			{
				Solver->GetRigidClustering().DeactivateClusterParticle(9);
			}
			if (Frame == 4)
			{
				Solver->GetRigidClustering().DeactivateClusterParticle(8);
			}

			//UE_LOG(GCTCL_Log, Verbose, TEXT("FRAME : %d"), Frame);
			//for (int32 rdx = 0; rdx < (int32)Particles.Size(); rdx++)
			//{
			//	UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...Disabled[%d] : %d"), rdx, Particles.Disabled(rdx));
			//	UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...    InvM[%d] : %f"), rdx, Particles.InvM(rdx));
			//}

			EXPECT_EQ(Particles.InvM(0), 0.f); // floor
			EXPECT_NE(Particles.InvM(1), 0.f); // dynamic rigid
			EXPECT_EQ(Particles.InvM(2), 0.f); // kinematic rigid
			EXPECT_NE(Particles.InvM(3), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(4), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(5), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(6), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(7), 0.f); // dynamic cluster

			FVector Ref1, Ref2, Ref7; // RigidBody0(Dynamic), RigidBody1(Kinematic), RigidBody6(Kinematic then Dynamic)
			if (Conditions[0] == false && Frame == 1)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == true &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == false)
				{
					Conditions[0] = true;
					Ref1 = Particles.X(1);
					Ref2 = Particles.X(2);
					Ref7 = Particles.X(7);
					EXPECT_EQ(Particles.InvM(8), 0.f); // kinematic cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Kinematic); // kinematic cluster
					EXPECT_EQ(Particles.InvM(9), 0.f); // kinematic cluster
					EXPECT_EQ(Particles.ObjectState(9), Chaos::EObjectStateType::Kinematic); // kinematic cluster
				}
			}
			else if (Conditions[0] == true && Conditions[1] == false && Frame == 2)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == false &&
					Particles.Disabled(9) == true)
				{
					Conditions[1] = true;
					FVector X1 = Particles.X(1);
					FVector X2 = Particles.X(2);
					FVector X7 = Particles.X(7);

					EXPECT_NEAR(FMath::Abs(X1.Size() - Ref1.Size()), 0, KINDA_SMALL_NUMBER) << *FString("Kinematic body1 moved");
					EXPECT_NEAR(FMath::Abs(X2.Size() - Ref2.Size()), 0, KINDA_SMALL_NUMBER) << *FString("Kinematic body2 moved");
					EXPECT_NEAR(FMath::Abs(X7.Size() - Ref7.Size()), 0, KINDA_SMALL_NUMBER) << *FString("Kinematic body7 moved");
					EXPECT_EQ(Particles.InvM(8), 0.f); // kinematic cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Kinematic); // kinematic cluster
					EXPECT_EQ(Particles.InvM(9), 0.f); 
					EXPECT_EQ(Particles.ObjectState(9), Chaos::EObjectStateType::Kinematic);

					EXPECT_EQ(ClusterMap.Num(), 3);
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 8, { 7,2 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));
				}
			}
			else if (Conditions[1] == true && Conditions[2] == false && Frame == 4)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == false &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == false &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == true)
				{
					Conditions[2] = true;
					FVector X1 = Particles.X(1);
					FVector X2 = Particles.X(2);
					FVector X7 = Particles.X(7);

					EXPECT_GT(FMath::Abs(X1.Size() - Ref1.Size()), KINDA_SMALL_NUMBER) << *FString("Dynamic body failed to move");
					EXPECT_NEAR(FMath::Abs(X2.Size() - Ref2.Size()), 0, KINDA_SMALL_NUMBER) << *FString("Kinematic body moved");
					EXPECT_NEAR(FMath::Abs(X7.Size() - Ref7.Size()), 0, KINDA_SMALL_NUMBER) << FString("Kinematic body moved");
					EXPECT_EQ(Particles.InvM(8), 0.f); // kinematic cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Kinematic); // kinematic cluster

					EXPECT_EQ(ClusterMap.Num(), 2);
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));
				}
			}
			else if (Conditions[2] == true && Conditions[3] == false && Frame == 6)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == false &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == false &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == true)
				{
					Conditions[3] = true;
					FVector X1 = Particles.X(1);
					FVector X2 = Particles.X(2);
					FVector X7 = Particles.X(7);

					EXPECT_GT(FMath::Abs(X1.Size() - Ref1.Size()), KINDA_SMALL_NUMBER) << *FString("Dynamic body 1 failed to move.");
					EXPECT_NEAR(FMath::Abs(X2.Size() - Ref2.Size()), 0, KINDA_SMALL_NUMBER) << *FString("Kinematic body moved");
					EXPECT_GT(FMath::Abs(X7.Size() - Ref7.Size()), KINDA_SMALL_NUMBER) << *FString("Dynamic body 7 failed to move");
					EXPECT_EQ(Particles.InvM(8), 0.f); // kinematic cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Kinematic); // kinematic cluster


					EXPECT_EQ(ClusterMap.Num(), 2);
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 6,3 }));
					EXPECT_TRUE(ClusterMapContains(ClusterMap, 6, { 5,4 }));
				}
			}
		}
		for (int i = 0; i < Conditions.Num(); i++)
		{
			EXPECT_TRUE(Conditions[i]);
		}
#endif
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

		
	}
	template void RigidBodies_ClusterTest_KinematicAnchor<float>();


	template<class T>
	void RigidBodies_ClusterTest_StaticAnchor()
	{
		// Test : Set one element static. When the cluster breaks the elements that do not contain the static
		//        rigid body should be dynamic, while the clusters that contain the static body should remain 
		//        static. 
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(20.f)), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(40.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(50.f)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(60.f)), FVector(1.0)));

		RestCollection->AddElements(4, FGeometryCollection::TransformGroup);
		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[5] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[6] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[7] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[8] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 5, { 4,3 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 6, { 5,2 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 7, { 6,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 8, { 7,0 });

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);
		DynamicCollection->GetAttribute<int32>("DynamicState", FGeometryCollection::TransformGroup)[1] = (uint8)EObjectStateTypeEnum::Chaos_Object_Static;

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.DamageThreshold = { 50.0, 50.0, 50.0, FLT_MAX };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();

#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float StartingRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size(), CurrentRigidDistance = 0.f;

		TArray<bool> Conditions = { false,false,false,false };

		Solver->AdvanceSolverBy(1 / 24.);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Clustering.GetChildrenMap();

		for (int Frame = 1; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

			if (Frame == 2)
			{
				Solver->GetRigidClustering().DeactivateClusterParticle(9);
			}
			if (Frame == 4)
			{
				Solver->GetRigidClustering().DeactivateClusterParticle(8);
			}

			//UE_LOG(GCTCL_Log, Verbose, TEXT("FRAME : %d"), Frame);
			//for (int32 rdx = 0; rdx < (int32)Particles.Size(); rdx++)
			//{
			//	UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...Disabled[%d] : %d"), rdx, Particles.Disabled(rdx));
			//	UE_LOG(GCTCL_Log, Verbose, TEXT("... ... ...    InvM[%d] : %f"), rdx, Particles.InvM(rdx));
			//}

			EXPECT_EQ(Particles.InvM(0), 0.f); // floor
			EXPECT_NE(Particles.InvM(1), 0.f); // dynamic rigid
			EXPECT_EQ(Particles.InvM(2), 0.f); // static rigid
			EXPECT_NE(Particles.InvM(3), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(4), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(5), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(6), 0.f); // dynamic rigid
			EXPECT_NE(Particles.InvM(7), 0.f); // dynamic cluster

			FVector Ref1, Ref2, Ref7; // RigidBody0(Dynamic), RigidBody1(static), RigidBody6(static then Dynamic)
			if (Conditions[0] == false && Frame == 1)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == true &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == false)
				{
					Conditions[0] = true;
					Ref1 = Particles.X(1);
					Ref2 = Particles.X(2);
					Ref7 = Particles.X(7);
					EXPECT_EQ(Particles.InvM(8), 0.f); // static cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Static); // Static cluster
					EXPECT_EQ(Particles.InvM(9), 0.f); // static cluster
					EXPECT_EQ(Particles.ObjectState(9), Chaos::EObjectStateType::Static); // Static cluster
				}
			}
			else if (Conditions[0] == true && Conditions[1] == false && Frame == 2)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == true &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == true &&
					Particles.Disabled(8) == false &&
					Particles.Disabled(9) == true)
				{
					Conditions[1] = true;
					FVector X1 = Particles.X(1);
					FVector X2 = Particles.X(2);
					FVector X7 = Particles.X(7);

					EXPECT_NEAR(X1.Size() - Ref1.Size(), 0, KINDA_SMALL_NUMBER) << *FString("static body1 moved");
					EXPECT_NEAR(X2.Size() - Ref2.Size(), 0, KINDA_SMALL_NUMBER) << *FString("static body2 moved");
					EXPECT_NEAR(X7.Size() - Ref7.Size(), 0, KINDA_SMALL_NUMBER) << *FString("static body7 moved");
					EXPECT_EQ(Particles.InvM(8), 0.f); // static cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Static); // static cluster
					EXPECT_EQ(Particles.InvM(9), 0.f);
					EXPECT_EQ(Particles.ObjectState(9), Chaos::EObjectStateType::Static);
				}
			}
			else if (Conditions[1] == true && Conditions[2] == false && Frame == 4)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == false &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == false &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == true)
				{
					Conditions[2] = true;
					FVector X1 = Particles.X(1);
					FVector X2 = Particles.X(2);
					FVector X7 = Particles.X(7);

					EXPECT_GT(FMath::Abs(X1.Size() - Ref1.Size()), KINDA_SMALL_NUMBER) << *FString("Dynamic body failed to move");
					EXPECT_NEAR(FX2.Size() - Ref2.Size(), 0, KINDA_SMALL_NUMBER) << *FString("static body moved");
					EXPECT_NEAR(X7.Size() - Ref7.Size(), 0, KINDA_SMALL_NUMBER) << *FString("static body moved");
					EXPECT_EQ(Particles.InvM(8), 0.f); // static cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Static); // static cluster
				}
			}
			else if (Conditions[2] == true && Conditions[3] == false && Frame == 6)
			{
				if (Particles.Disabled(0) == false &&
					Particles.Disabled(1) == false &&
					Particles.Disabled(2) == false &&
					Particles.Disabled(3) == true &&
					Particles.Disabled(4) == true &&
					Particles.Disabled(5) == true &&
					Particles.Disabled(6) == true &&
					Particles.Disabled(7) == false &&
					Particles.Disabled(8) == true &&
					Particles.Disabled(9) == true)
				{
					Conditions[3] = true;
					FVector X1 = Particles.X(1);
					FVector X2 = Particles.X(2);
					FVector X7 = Particles.X(7);

					EXPECT_GT(FMath::Abs(X1.Size() - Ref1.Size()), KINDA_SMALL_NUMBER) << *FString("Dynamic body 1 failed to move.");
					EXPECT_NEAR(X2.Size() - Ref2.Size(), 0, KINDA_SMALL_NUMBER) << *FString("static body moved");
					EXPECT_GT(FMath::Abs(X7.Size() - Ref7.Size()), KINDA_SMALL_NUMBER) << *FString("Dynamic body 7 failed to move");
					EXPECT_EQ(Particles.InvM(8), 0.f); // static cluster
					EXPECT_EQ(Particles.ObjectState(8), Chaos::EObjectStateType::Static); // static cluster
				}
			}
		}
		for (int i = 0; i < Conditions.Num(); i++)
		{
			EXPECT_TRUE(Conditions[i]);
		}
#endif
#endif
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

	}
	template void RigidBodies_ClusterTest_StaticAnchor<float>();


	template<class T>
	void RigidBodies_ClusterTest_UnionClusters()
	{
		// Test : Set one element kinematic. When the cluster breaks the elements that do not contain the kinematic
		//        Rigid body should be dynamic, while the clusters that contain the kinematic body should remain kinematic. 
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody( FVector(0,0,100) );
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		TSharedPtr<FGeometryCollection> RestCollection2 = CreateClusteredBody( FVector(0,0,200) );
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection2 = GeometryCollectionToGeometryDynamicCollection(RestCollection2.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		//DynamicCollection->GetAttribute<int32>("DynamicState", FGeometryCollection::TransformGroup)[1] = (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic;

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 1;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		auto InitFunc2 = [&RestCollection2, &DynamicCollection2, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection2.Get();
			InParams.DynamicCollection = DynamicCollection2.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 1;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection2, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FGeometryCollectionPhysicsProxy* PhysObject2 = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection2.Get(), InitFunc2, nullptr, nullptr);;
		PhysObject2->Initialize();


		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(PhysObject2);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();
		PhysObject2->ActivateBodies();

		TArray<float> Distances;
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		TManagedArray<FTransform>& Transform2 = DynamicCollection2->Transform;

		for (int Frame = 0; Frame < 100; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);
#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
			const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
			const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();
#endif

			if (Frame == 0)
			{

				TArray<FTransform> GlobalTransform;
				GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, GlobalTransform);

				TArray<FTransform> GlobalTransform2;
				GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, GlobalTransform2);

				// build relative transforms distances
				for (int32 i = 0; i < (int32)GlobalTransform.Num()-1; i++)
				{
					for (int j = 0; j < (int32)GlobalTransform2.Num()-1; j++)
					{
						Distances.Add((GlobalTransform[i].GetTranslation() - GlobalTransform2[j].GetTranslation()).Size());
					}
				}

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
				EXPECT_EQ(ClusterMap.Num(), 1);
				EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 1,2,5,4 }));
#endif
			}
		}

		TArray<FTransform> GlobalTransform;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, GlobalTransform);

		TArray<FTransform> GlobalTransform2;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, GlobalTransform2);

		// build relative transforms distances
		TArray<float> Distances2;
		for (int32 i = 0; i < (int32)GlobalTransform.Num() - 1; i++)
		{
			for (int j = 0; j < (int32)GlobalTransform2.Num() - 1; j++)
			{
				Distances2.Add((GlobalTransform[i].GetTranslation() - GlobalTransform2[j].GetTranslation()).Size());
			}
		}
		for (int i = 0; i < Distances.Num()/2.0; i++)
		{
			EXPECT_LT( FMath::Abs(Distances[i] - Distances2[i]), 0.1 );
		}


		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		delete PhysObject;
		delete PhysObject2;

	}
	template void RigidBodies_ClusterTest_UnionClusters<float>();





	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredNode()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody(FVector(0, 0, 100));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		TSharedPtr<FGeometryCollection> RestCollection2 = CreateClusteredBody(FVector(0, 0, 200));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection2 = GeometryCollectionToGeometryDynamicCollection(RestCollection2.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		DynamicCollection->GetAttribute<int32>("DynamicState", FGeometryCollection::TransformGroup)[1] = (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic;
 
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 1;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			InParams.ClusterConnectionMethod = Chaos::FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		auto InitFunc2 = [&RestCollection2, &DynamicCollection2, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection2.Get();
			InParams.DynamicCollection = DynamicCollection2.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 1;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			InParams.ClusterConnectionMethod = Chaos::FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection2, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FGeometryCollectionPhysicsProxy* PhysObject2 = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection2.Get(), InitFunc2, nullptr, nullptr);;
		PhysObject2->Initialize();


		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
#endif
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(PhysObject2);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();
		PhysObject2->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		TArray<FTransform> InitialGlobalTransforms;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, InitialGlobalTransforms);
		TArray<FTransform> InitialGlobalTransforms2;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, InitialGlobalTransforms2);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::TArrayCollectionArray<Chaos::ClusterId> & ClusterIdsArray = Solver->GetRigidClustering().GetClusterIdsArray();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();

		EXPECT_EQ(ClusterMap.Num(), 1);
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 7, { 1,2,4,5}));
#endif

		for (int Frame = 1; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			if (Frame == 5)
			{
				Solver->GetRigidClustering().ReleaseClusterParticles({4,5});
			}
#endif

			FinalizeSolver(*Solver);

			if (Frame < 5)
			{
				TArray<FTransform> GlobalTransforms2;
				GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, GlobalTransforms2);
				for (int i = 0; i < GlobalTransforms2.Num(); i++)
				{
					EXPECT_LT((GlobalTransforms2[i].GetTranslation() - InitialGlobalTransforms2[i].GetTranslation()).Size(),KINDA_SMALL_NUMBER);
				}
			}
			TArray<FTransform> GlobalTransforms;
			GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, GlobalTransforms);
			for (int i = 0; i < GlobalTransforms.Num(); i++)
			{
				EXPECT_LT((GlobalTransforms[i].GetTranslation() - InitialGlobalTransforms[i].GetTranslation()).Size(),KINDA_SMALL_NUMBER);
			}

		}

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		EXPECT_EQ(ClusterMap.Num(), 1);
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 8, { 1,2 }));
#endif

		TArray<int32> Subset({ 1 });
		TArray<FTransform> GlobalTransform2;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, Subset, GlobalTransform2);
		EXPECT_GT((GlobalTransform2[0].GetTranslation() - InitialGlobalTransforms2[Subset[0]].GetTranslation()).Size(), SMALL_NUMBER);
		EXPECT_LT(GlobalTransform2[0].GetTranslation().Z, InitialGlobalTransforms2[Subset[0]].GetTranslation().Z);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete PhysObject2;

	}
	template void RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredNode<float>();

	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredKinematicNode()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody(FVector(0, 0, 100));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		TSharedPtr<FGeometryCollection> RestCollection2 = CreateClusteredBody(FVector(0, 0, 200));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection2 = GeometryCollectionToGeometryDynamicCollection(RestCollection2.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);

		DynamicCollection->GetAttribute<int32>("DynamicState", FGeometryCollection::TransformGroup)[1] = (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic;

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 1;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			InParams.ClusterConnectionMethod = Chaos::FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		auto InitFunc2 = [&RestCollection2, &DynamicCollection2, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection2.Get();
			InParams.DynamicCollection = DynamicCollection2.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 1;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			InParams.ClusterConnectionMethod = Chaos::FClusterCreationParameters<T>::EConnectionMethod::DelaunayTriangulation;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection2, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();

		FGeometryCollectionPhysicsProxy* PhysObject2 = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection2.Get(), InitFunc2, nullptr, nullptr);;
		PhysObject2->Initialize();

		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
#endif

#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(PhysObject2);
#endif
		Solver->SetHasFloor(true);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();
		PhysObject2->ActivateBodies();

		TArray<float> Distances;
		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		TArray<FTransform> InitialGlobalTransforms;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, InitialGlobalTransforms);
		TArray<FTransform> InitialGlobalTransforms2;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, InitialGlobalTransforms2);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::TArrayCollectionArray<Chaos::ClusterId> & ClusterIdsArray = Solver->GetRigidClustering().GetClusterIdsArray();
#endif

		for (int Frame = 1; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			if (Frame == 5)
			{
				Solver->GetRigidClustering().ReleaseClusterParticles({ 4,5 });
			}
#endif

			FinalizeSolver(*Solver);

			// the cluster from DynamicCollection will always be kinematic, and will be released from the union. 
			TArray<FTransform> GlobalTransforms;
			GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, GlobalTransforms);
			for (int i = 0; i < GlobalTransforms.Num(); i++)
			{
				EXPECT_LT((GlobalTransforms[i].GetTranslation() - InitialGlobalTransforms[i].GetTranslation()).Size(),KINDA_SMALL_NUMBER);
			}

			// the cluster from DynamicCollection2 will always be dynamic after its released from the union, but should be
			// kinematic before the release. 
			if (Frame < 5)
			{
				TArray<FTransform> GlobalTransforms2;
				GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, GlobalTransforms2);
				for (int i = 0; i < GlobalTransforms2.Num(); i++)
				{
					EXPECT_LT((GlobalTransforms2[i].GetTranslation() - InitialGlobalTransforms2[i].GetTranslation()).Size(),KINDA_SMALL_NUMBER);
				}
			}
		}

		// validate that DynamicCollection2 became dynamic and fell from the cluster. 
		TArray<int32> Subset({ 1 });
		TArray<FTransform> GlobalTransform2;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection2->Transform, DynamicCollection2->Parent, Subset, GlobalTransform2);
		EXPECT_GT((GlobalTransform2[0].GetTranslation() - InitialGlobalTransforms2[Subset[0]].GetTranslation()).Size(), SMALL_NUMBER);
		EXPECT_LT(GlobalTransform2[0].GetTranslation().Z, InitialGlobalTransforms2[Subset[0]].GetTranslation().Z);

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete PhysObject2;
	
	}
	template void RigidBodies_ClusterTest_ReleaseClusterParticle_ClusteredKinematicNode<float>();
#endif

	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticles_AllLeafNodes()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody(FVector(0, 0, 100));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);
		DynamicCollection->GetAttribute<int32>("DynamicState", FGeometryCollection::TransformGroup)[1] = (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic;

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 0;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
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

		TArray<FTransform> InitialGlobalTransforms;
		GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, InitialGlobalTransforms);
		float PreviousHeight = InitialGlobalTransforms[0].GetTranslation().Y;

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#endif
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::TArrayCollectionArray<Chaos::ClusterId> & ClusterIdsArray = Solver->GetRigidClustering().GetClusterIdsArray();
#endif

		for (int Frame = 1; Frame < 10; Frame++)
		{
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			if (Frame == 5)
			{
				Solver->GetRigidClustering().ReleaseClusterParticles({ 0,1 });
			}
#endif

			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);


			// cluster should be sleeping until the break
			TArray<FTransform> GlobalTransforms;
			GeometryCollectionAlgo::GlobalMatrices(DynamicCollection->Transform, DynamicCollection->Parent, GlobalTransforms);
			if (Frame < 5)
			{
				for (int i = 0; i < GlobalTransforms.Num(); i++)
				{
					EXPECT_LT((GlobalTransforms[i].GetTranslation() - InitialGlobalTransforms[i].GetTranslation()).Size(), KINDA_SMALL_NUMBER);
				}
			}
			else if (Frame <= 7)
			{
				EXPECT_GT(PreviousHeight, GlobalTransforms[0].GetTranslation().Z);
				EXPECT_LT(FMath::Abs(InitialGlobalTransforms[1].GetTranslation().Z - GlobalTransforms[1].GetTranslation().Z), KINDA_SMALL_NUMBER);
			}
			PreviousHeight = GlobalTransforms[0].GetTranslation().Z;
		}
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

		
	}
	template void RigidBodies_ClusterTest_ReleaseClusterParticles_AllLeafNodes<float>();

	template<class T>
	void RigidBodies_ClusterTest_ReleaseClusterParticles_ClusterNodeAndSubClusterNode()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = CreateClusteredBody_TwoParents_TwoBodies(FVector(0, 0, 100));
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), (uint8)EObjectStateTypeEnum::Chaos_Object_Dynamic);
		DynamicCollection->GetAttribute<int32>("DynamicState", FGeometryCollection::TransformGroup)[1] = (uint8)EObjectStateTypeEnum::Chaos_Object_Kinematic;

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial  = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.MaxClusterLevel = 1;
			InParams.ClusterGroupIndex = 1;
			InParams.DamageThreshold = { FLT_MAX };
			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};


		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();


		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
#endif

#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		const Chaos::TPBDRigidParticles<float, 3>& Particles = PhysObject->GetSolver()->GetRigidParticles();
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::TArrayCollectionArray<Chaos::ClusterId> & ClusterIdsArray = Solver->GetRigidClustering().GetClusterIdsArray();
		const Chaos::FPBDRigidsSolver::FClusteringType::FClusterMap & ClusterMap = Solver->GetRigidClustering().GetChildrenMap();

		EXPECT_EQ(ClusterMap.Num(), 2);
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 2, { 0,1 }));
		EXPECT_TRUE(ClusterMapContains(ClusterMap, 4, { 2 }));


		for (int Frame = 1; Frame < 10; Frame++)
		{
			Solver->AdvanceSolverBy(1 / 24.);
			FinalizeSolver(*Solver);

			if (Frame == 5)
			{
				Solver->GetRigidClustering().ReleaseClusterParticles({ 2 });
			}


			if (Frame < 5)
			{
				EXPECT_TRUE(Particles.Disabled(2));
				EXPECT_NE(ClusterIdsArray[2].Id, INDEX_NONE);
				EXPECT_EQ(ClusterIdsArray[3].Id, INDEX_NONE);
				EXPECT_EQ(ClusterIdsArray[4].Id, INDEX_NONE);
			}
			else
			{
				EXPECT_TRUE(!Particles.Disabled(2));
				EXPECT_EQ(ClusterIdsArray[2].Id, INDEX_NONE);
				EXPECT_EQ(ClusterIdsArray[3].Id, INDEX_NONE);
				EXPECT_EQ(ClusterIdsArray[4].Id, INDEX_NONE);

				EXPECT_EQ(ClusterMap.Num(), 1);
				EXPECT_TRUE(ClusterMapContains(ClusterMap, 2, { 0,1 }));
			}
		}
#endif
#endif

		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;

	}
	template void RigidBodies_ClusterTest_ReleaseClusterParticles_ClusterNodeAndSubClusterNode<float>();

	template<class T>
	void RigidBodies_ClusterTest_RemoveOnFracture()
	{
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
		InitMaterialToZero(PhysicalMaterial);

		TSharedPtr<FGeometryCollection> RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, -10, 10)), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0, 0, 0.)), FVector(0, 10, 10)), FVector(1.0)));
		EXPECT_EQ(RestCollection->Transform.Num(), 2);

		// this transform should have a zero scale after the simulation has run to the point of fracture
		RestCollection->SetFlags(1, FGeometryCollection::FS_RemoveOnFracture);

		FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(RestCollection.Get());
		EXPECT_EQ(RestCollection->Transform.Num(), 3);
		RestCollection->Transform[2] = FTransform(FQuat::MakeFromEuler(FVector(90.f, 0, 0.)), FVector(0, 0, 40));

		TSharedPtr<FGeometryDynamicCollection> DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get());

		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
			InParams.Shared.SizeSpecificData[0].ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			InParams.DamageThreshold = { 0.1f };
			InParams.Simulating = true;
			InParams.RemoveOnFractureEnabled = true; // <--- the feature we are testing
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FRadialFalloff * FalloffField = new FRadialFalloff();
		FalloffField->Magnitude = 10.5;
		FalloffField->Radius = 100.0;
		FalloffField->Position = FVector(0.0, 0.0, 0.0);
		FalloffField->Falloff = EFieldFalloffType::Field_FallOff_None;

		FFieldSystemPhysicsProxy* FieldObject = new FFieldSystemPhysicsProxy(nullptr);

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);
		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::FPBDRigidsSolver::FClusteringType & Clustering = Solver->GetRigidClustering();
#endif

		PhysObject->Initialize();

#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(PhysObject);
		Solver->RegisterObject(FieldObject);
#endif
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
		PhysObject->ActivateBodies();

		TManagedArray<FTransform>& Transform = DynamicCollection->Transform;
		float StartingRigidDistance = (Transform[1].GetTranslation() - Transform[0].GetTranslation()).Size(), CurrentRigidDistance = 0.f;

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		Chaos::TArrayCollectionArray<float>& InternalStrain = Clustering.GetStrainArray();
#endif

		FName TargetName = GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain);
		FFieldSystemCommand Command(TargetName, FalloffField->NewCopy());
		FFieldSystemMetaDataProcessingResolution* ResolutionData = new FFieldSystemMetaDataProcessingResolution(EFieldResolutionType::Field_Resolution_Maximum);
		Command.MetaData.Add(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution, TUniquePtr< FFieldSystemMetaDataProcessingResolution >(ResolutionData));
		FieldObject->BufferCommand(Solver, Command);

		FVector Scale = Transform[1].GetScale3D();

		EXPECT_NEAR(Scale.X, 1.0f, SMALL_NUMBER);
		EXPECT_NEAR(Scale.Y, 1.0f, SMALL_NUMBER);
		EXPECT_NEAR(Scale.Z, 1.0f, SMALL_NUMBER);

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		FieldObject->BufferCommand(Solver, { TargetName, FalloffField->NewCopy() });

		Solver->AdvanceSolverBy(1 / 24.);
		FinalizeSolver(*Solver);

		FVector Scale2 = Transform[1].GetScale3D();
		// geometry hidden by 0 scaling on transform
		EXPECT_NEAR(Scale2.X, 0.0f, SMALL_NUMBER);
		EXPECT_NEAR(Scale2.Y, 0.0f, SMALL_NUMBER);
		EXPECT_NEAR(Scale2.Z, 0.0f, SMALL_NUMBER);
			
		FChaosSolversModule::GetModule()->DestroySolver(Solver);

		delete PhysObject;
		delete FalloffField;
		delete FieldObject;
	}
	template void RigidBodies_ClusterTest_RemoveOnFracture<float>();

	template<class T>
	void RigidBodiess_ClusterTest_ParticleImplicitCollisionGeometry()
	{
		typename SimulationObjects<T>::FParameters P;
		P.CollisionGroup = -1;
		P.EnableClustering = true;
		P.SizeData.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_LevelSet;
		P.SizeData.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
		P.SizeData.MinLevelSetResolution = 15;
		P.SizeData.MaxLevelSetResolution = 20;

		SimulationObjects<T>* Object = new SimulationObjects<T>(P, CreateClusteredBody_FracturedGeometry());
		Object->PhysicsProxy->Initialize();
		Object->PhysicsProxy->ActivateBodies();

		typedef TUniquePtr<Chaos::TImplicitObject<float, 3>> FImplicitPointer;
		const TManagedArray<FImplicitPointer> & Implicits = Object->RestCollection->template GetAttribute<FImplicitPointer>(FGeometryCollectionPhysicsProxy::ImplicitsAttribute, FTransformCollection::TransformGroup);

		typedef TUniquePtr< FCollisionStructureManager::FSimplicial > FSimplicialPointer;
		const TManagedArray<FSimplicialPointer> & Simplicials = Object->RestCollection->template GetAttribute<FSimplicialPointer>(FGeometryCollectionPhysicsProxy::SimplicialsAttribute, FTransformCollection::TransformGroup);


		Chaos::FPBDRigidsSolver* Solver = FChaosSolversModule::GetModule()->CreateSolver(true);
		Solver->SetHasFloor(false);
		Solver->SetEnabled(true);
#if CHAOS_PARTICLEHANDLE_TODO
		Solver->RegisterObject(Object->PhysicsProxy.Get());
#endif

		Solver->AdvanceSolverBy(1 / 24.);

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		Chaos::TPBDRigidParticles<float, 3>& Particles = Solver->GetRigidParticles();
#endif

		float CollisionParticlesPerObjectFractionDefault = 0.5f;
		IConsoleVariable*  CVarCollisionParticlesPerObjectFractionDefault = IConsoleManager::Get().FindConsoleVariable(TEXT("p.CollisionParticlesPerObjectFractionDefault"));
		EXPECT_NE(CVarCollisionParticlesPerObjectFractionDefault, nullptr);
		if (CVarCollisionParticlesPerObjectFractionDefault != nullptr)
		{
			CollisionParticlesPerObjectFractionDefault = CVarCollisionParticlesPerObjectFractionDefault->GetFloat();
		}

#if TODO_REIMPLEMENT_GET_RIGID_PARTICLES
		EXPECT_EQ(Particles.CollisionParticles(Object->PhysicsProxy->RigidBodyIDArray_TestingAccess()[10])->Size(), (int)(Simplicials[10]->Size() * CollisionParticlesPerObjectFractionDefault));
		EXPECT_EQ(Particles.CollisionParticles(Object->PhysicsProxy->RigidBodyIDArray_TestingAccess()[11])->Size(), (int)(Simplicials[11]->Size() * CollisionParticlesPerObjectFractionDefault));
		EXPECT_EQ(Particles.CollisionParticles(Object->PhysicsProxy->RigidBodyIDArray_TestingAccess()[12])->Size(), (int)(Simplicials[12]->Size() * CollisionParticlesPerObjectFractionDefault));
#endif

		// cleanup
		//for (auto Obj : Collections) delete Obj;
		FChaosSolversModule::GetModule()->DestroySolver(Solver);
		
		delete Object;
	}
	template void RigidBodiess_ClusterTest_ParticleImplicitCollisionGeometry<float>();
}
