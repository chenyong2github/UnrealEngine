// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionExampleUtility.h"
#include "UObject/UObjectGlobals.h"
#include "GeometryCollection/GeometryDynamicCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

#include "PBDRigidsSolver.h"
#include "PhysicsProxy/PhysicsProxies.h"
#include "../Resource/FracturedGeometry.h"


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
#if INCLUDE_CHAOS
		InSolver.ForEachPhysicsProxy([](auto* Object)
		{
			Object->BufferPhysicsResults();
			Object->FlipBuffer();
			Object->PullFromPhysicsState();
		});
#endif
	}




	TSharedPtr<FGeometryCollection>	CreateClusteredBody(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
#if INCLUDE_CHAOS
		RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), Position), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));

		RestCollection->AddElements(1, FGeometryCollection::TransformGroup);
		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[2] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 2, { 0,1 });

#endif
		return RestCollection;
	}

	TSharedPtr<FGeometryCollection>	CreateClusteredBody_TwoParents_TwoBodies(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
#if INCLUDE_CHAOS
		RestCollection = GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), Position), FVector(1.0));
		RestCollection->AppendGeometry(*GeometryCollection::MakeCubeElement(FTransform(FQuat::MakeFromEuler(FVector(0.f)), FVector(30.f)), FVector(1.0)));

		RestCollection->AddElements(2, FGeometryCollection::TransformGroup);

		// @todo(ClusteringUtils) This is a bad assumption, the state flags should be initialized to zero.
		RestCollection->SimulationType[2] = FGeometryCollection::ESimulationTypes::FST_Clustered;
		RestCollection->SimulationType[3] = FGeometryCollection::ESimulationTypes::FST_Clustered;

		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 2, { 0,1 });
		GeometryCollectionAlgo::ParentTransforms(RestCollection.Get(), 3, { 2 });

#endif
		return RestCollection;
	}

	TSharedPtr<FGeometryCollection>	CreateClusteredBody_FourParents_OneBody(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
#if INCLUDE_CHAOS
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

#endif
		return RestCollection;
	}



	TSharedPtr<FGeometryCollection>	CreateClusteredBody_TwoByTwo_ThreeTransform(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
#if INCLUDE_CHAOS
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

#endif
		return RestCollection;
	}



	TSharedPtr<FGeometryCollection>	CreateClusteredBody_ThreeByTwo_ThreeTransform(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
#if INCLUDE_CHAOS
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

#endif
		return RestCollection;
	}


	TSharedPtr<FGeometryCollection>	CreateClusteredBody_FracturedGeometry(FVector Position)
	{
		TSharedPtr<FGeometryCollection> RestCollection;
#if INCLUDE_CHAOS

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

#endif
		return RestCollection;
	}

}

