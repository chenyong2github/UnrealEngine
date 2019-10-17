// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleUtility.h"
#include "UObject/UObjectGlobals.h"
#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionSimulationCoreTypes.h"

#include "PBDRigidsSolver.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "../Resource/FracturedGeometry.h"

#include "Chaos/ErrorReporter.h"


namespace GeometryCollectionExample {

	TSharedPtr<FGeometryDynamicCollection> GeometryCollectionToGeometryDynamicCollection(const FGeometryCollection* InputCollection, int DynamicStateDefault)
	{
		TSharedPtr<FGeometryDynamicCollection> NewCollection(new FGeometryDynamicCollection());
		NewCollection->CopyAttribute(*InputCollection, FTransformCollection::TransformAttribute, FGeometryCollection::TransformGroup);
		NewCollection->CopyAttribute(*InputCollection, FTransformCollection::ParentAttribute, FGeometryCollection::TransformGroup);
		NewCollection->CopyAttribute(*InputCollection, FTransformCollection::ChildrenAttribute, FGeometryCollection::TransformGroup);
		NewCollection->CopyAttribute(*InputCollection, FGeometryCollection::SimulationTypeAttribute, FGeometryCollection::TransformGroup);
		NewCollection->CopyAttribute(*InputCollection, FGeometryCollection::StatusFlagsAttribute, FGeometryCollection::TransformGroup);

		for (int i = 0; i < NewCollection->NumElements(FTransformCollection::TransformGroup);i++)
		{
			NewCollection->DynamicState[i] = DynamicStateDefault;
			NewCollection->Active[i] = true ;
		}

		NewCollection->SyncAllGroups(*InputCollection);
		return NewCollection;
	}

	void FinalizeSolver(Chaos::FPBDRigidsSolver& InSolver)
	{
		InSolver.ForEachPhysicsProxy([](auto* Object)
		{
			Object->BufferPhysicsResults();
			Object->FlipBuffer();
			Object->PullFromPhysicsState();
		});
	}




	TSharedPtr<FGeometryCollection>	CreateClusteredBody(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
		RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), Position), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));

		RestCollection->AddElements(1, FGeometryCollection::TransformGroup);
		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[2] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 2, { 0,1 });

		return RestCollection;
	}

	TSharedPtr<FGeometryCollection>	CreateClusteredBody_TwoParents_TwoBodies(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
		RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), Position), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));

		RestCollection->AddElements(2, FGeometryCollection::TransformGroup);

		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[2] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[3] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 2, { 0,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 3, { 2 });

		return RestCollection;
	}

	TSharedPtr<FGeometryCollection>	CreateClusteredBody_FourParents_OneBody(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
		RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), Position), FVector(1.0));

		RestCollection->AddElements(4, FGeometryCollection::TransformGroup);

		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[1] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[2] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[3] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[4] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 1, { 0 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 2, { 1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 3, { 2 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 4, { 3 });

		return RestCollection;
	}



	TSharedPtr<FGeometryCollection>	CreateClusteredBody_TwoByTwo_ThreeTransform(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
		RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(0,0,0)), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(100,0,0)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(200,0,0)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(300,0,0)), FVector(1.0)));

		RestCollection->AddElements(3, FGeometryCollection::TransformGroup);
		RestCollection->Transform[6].SetTranslation(Position);

		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[0] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[1] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[2] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[3] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[4] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[5] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[6] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 4, { 0,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 5, { 2,3 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 6, { 4,5 });

		return RestCollection;
	}



	TSharedPtr<FGeometryCollection>	CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
		RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(0, 0, 0)), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(100, 0, 0)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(200, 0, 0)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(300, 0, 0)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(400, 0, 0)), FVector(1.0)));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(500, 0, 0)), FVector(1.0)));

		RestCollection->AddElements(3, FGeometryCollection::TransformGroup);
		RestCollection->Transform[8].SetTranslation(Position);

		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[0] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[1] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[2] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[3] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[4] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[5] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[6] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[7] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[8] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 6, { 0,1,2 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 7, { 3,4,5 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 8, { 6,7 });

		return RestCollection;
	}


	TSharedPtr<FGeometryCollection>	CreateClusteredBody_FracturedGeometry(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;

		RestCollection = TSharedPtr<FGeometryCollection>(FGeometryCollection::NewGeometryCollection(
			FracturedGeometry::RawVertexArray,
			FracturedGeometry::RawIndicesArray,
			FracturedGeometry::RawBoneMapArray,
			FracturedGeometry::RawTransformArray,
			FracturedGeometry::RawLevelArray,
			FracturedGeometry::RawParentArray,
			FracturedGeometry::RawChildrenArray,
			FracturedGeometry::RawSimulationTypeArray,
			FracturedGeometry::RawStatusFlagsArray));


		GeometryCollectionAlgo::ReCenterGeometryAroundCentreOfMass(RestCollection.Get(), false);
		TArray<TArray<int32>> ConnectionGraph = RestCollection->ConnectionGraph();


		RestCollection->AddElements(2, FGeometryCollection::TransformGroup);

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(),11, { 1,2,5,6,7,8,10 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(),12, { 3,4,9 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 0, { 11,12 });

		for (int i = 0; i < RestCollection->NumElements(FGeometryCollection::TransformGroup); i++)
			RestCollection->SimulationType[i] = FGeometryCollection::ESimulationTypes::FST_Rigid;
		RestCollection->SimulationType[11] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[12] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[0] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		return RestCollection;
	}

	template<class T>
	void InitMaterialToZero(TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> const &PhysicalMaterial)
	{
		PhysicalMaterial->Friction = 0;
		PhysicalMaterial->Restitution = 0;
		PhysicalMaterial->SleepingLinearThreshold = 0;
		PhysicalMaterial->SleepingAngularThreshold = 0;
		PhysicalMaterial->DisabledLinearThreshold = 0;
		PhysicalMaterial->DisabledAngularThreshold = 0;
	}
	template void InitMaterialToZero(TUniquePtr<Chaos::TChaosPhysicsMaterial<float>> const &PhysicalMaterial);


	template<class T>
	void InitCollections(
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> &PhysicalMaterial,
		TSharedPtr<FGeometryCollection>& RestCollection,
		TSharedPtr<FGeometryDynamicCollection>& DynamicCollection,
		InitCollectionsParameters& InitParams
	)
	{
		// Allow for customized initialization of these objects in the calling function. 
		if (PhysicalMaterial == nullptr)
		{
			PhysicalMaterial = MakeUnique<Chaos::TChaosPhysicsMaterial<T>>();
			InitMaterialToZero(PhysicalMaterial);
		}

		if (RestCollection == nullptr)
		{
			//Default initialization is a cube of the specified center and size. 
			RestCollection = GeometryCollection::MakeCubeElement(InitParams.RestCenter, InitParams.RestScale);
			if (InitParams.RestInitFunc != nullptr)
			{
				InitParams.RestInitFunc(RestCollection);
			}
		}

		if (DynamicCollection == nullptr)
		{
			DynamicCollection = GeometryCollectionToGeometryDynamicCollection(RestCollection.Get(), InitParams.DynamicStateDefault);
		}
	}
	template void InitCollections(
		TUniquePtr<Chaos::TChaosPhysicsMaterial<float>> &PhysicalMaterial,
		TSharedPtr<FGeometryCollection>& RestCollection,
		TSharedPtr<FGeometryDynamicCollection>& DynamicCollection,
		InitCollectionsParameters& InitParams
	);

	template<class T>
	FGeometryCollectionPhysicsProxy* RigidBodySetup(
		TUniquePtr<Chaos::TChaosPhysicsMaterial<T>> &PhysicalMaterial,
		TSharedPtr<FGeometryCollection>& RestCollection,
		TSharedPtr<FGeometryDynamicCollection>& DynamicCollection,
		FInitFunc CustomFunc
	)
	{
#if INCLUDE_CHAOS
		auto InitFunc = [&RestCollection, &DynamicCollection, &PhysicalMaterial, &CustomFunc](FSimulationParameters& InParams)
		{
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = ECollisionTypeEnum::Chaos_Volumetric;

			if (CustomFunc != nullptr)
			{
				CustomFunc(InParams);
			}

			InParams.Simulating = true;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		};

		FGeometryCollectionPhysicsProxy* PhysObject = new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), InitFunc, nullptr, nullptr);;
		PhysObject->Initialize();
		return PhysObject;
#else
		return nullptr;
#endif
	}
	template FGeometryCollectionPhysicsProxy* RigidBodySetup(
		TUniquePtr<Chaos::TChaosPhysicsMaterial<float>> & PhysicalMaterial,
		TSharedPtr<FGeometryCollection>& RestCollection,
		TSharedPtr<FGeometryDynamicCollection>& DynamicCollection,
		FInitFunc CustomFunc
	);

} // end namespace GeometryCollectionExample

