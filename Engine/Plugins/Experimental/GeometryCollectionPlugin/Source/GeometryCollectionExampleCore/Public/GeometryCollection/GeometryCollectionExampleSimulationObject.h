// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosSolversModule.h"
#include "Chaos/Serializable.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/ErrorReporter.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Templates/SharedPointer.h"

class FGeometryCollection;
class FGeometryDynamicCollection;

namespace Chaos
{
	class FPBDRigidsSolver;
}

namespace GeometryCollectionExample {


	template<class T>
	class SimulationObjects
	{
	public:

		struct FParameters {
			FParameters()
				: CollisionGroup(0)
				, EnableClustering(false)
				, ClusterGroupIndex(0)
				, ClusterConnectionMethod(Chaos::FClusterCreationParameters<T>::EConnectionMethod::PointImplicit)
				, SizeData(FSharedSimulationSizeSpecificData())
			{
				SizeData.CollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
				SizeData.ImplicitType = EImplicitTypeEnum::Chaos_Implicit_Box;
			}
			int CollisionGroup;
			bool EnableClustering;
			int ClusterGroupIndex;
			typename Chaos::FClusterCreationParameters<T>::EConnectionMethod ClusterConnectionMethod;
			FSharedSimulationSizeSpecificData SizeData;
		};

		SimulationObjects(FParameters InParameters = FParameters(), TSharedPtr < FGeometryCollection > RestCollectionIn = GeometryCollection::MakeCubeElement(FTransform::Identity, FVector(1.0)))
			: Parameters(InParameters)
			, RestCollection(RestCollectionIn)
			, DynamicCollection(GeometryCollectionToGeometryDynamicCollection(RestCollection.Get()))
			, PhysicalMaterial(new Chaos::FChaosPhysicsMaterial())
			, PhysicsProxy(new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), [&](FSimulationParameters& P) {Init(P); }, nullptr, nullptr))
		{
			PhysicalMaterial->Friction = 0;
			PhysicalMaterial->Restitution = 0;
			PhysicalMaterial->SleepingLinearThreshold = 0;
			PhysicalMaterial->SleepingAngularThreshold = 0;
			PhysicalMaterial->DisabledLinearThreshold = 0;
			PhysicalMaterial->DisabledAngularThreshold = 0;
		}


		SimulationObjects(FParameters InParameters,
			TSharedPtr<FGeometryCollection> InRestCollection, 
			TSharedPtr<FGeometryDynamicCollection> InDynamicCollection, 
			TUniquePtr<Chaos::FChaosPhysicsMaterial> InPhysicalMaterial)
			: Parameters(InParameters)
			, RestCollection(InRestCollection)
			, DynamicCollection(InDynamicCollection)
			, PhysicalMaterial(MoveTemp(InPhysicalMaterial))
			, PhysicsProxy(new FGeometryCollectionPhysicsProxy(nullptr, DynamicCollection.Get(), [&](FSimulationParameters& P) {Init(P); }, nullptr, nullptr))
		{
		}

		void Init(FSimulationParameters& InParams)
		{
			PhysicsProxy->SetCollisionParticlesPerObjectFraction(1.0);
			InParams.RestCollection = RestCollection.Get();
			InParams.DynamicCollection = DynamicCollection.Get();
			InParams.PhysicalMaterial = MakeSerializable(PhysicalMaterial);
			InParams.Shared.SizeSpecificData[0].CollisionType = Parameters.SizeData.CollisionType;
			InParams.Shared.SizeSpecificData[0].ImplicitType = Parameters.SizeData.ImplicitType;
			InParams.Shared.SizeSpecificData[0].MaxLevelSetResolution = Parameters.SizeData.MaxLevelSetResolution;
			InParams.Shared.SizeSpecificData[0].MinLevelSetResolution = Parameters.SizeData.MinLevelSetResolution;
			InParams.Shared.SizeSpecificData[0].MaxClusterLevelSetResolution = Parameters.SizeData.MaxClusterLevelSetResolution;
			InParams.Shared.SizeSpecificData[0].MinClusterLevelSetResolution = Parameters.SizeData.MinClusterLevelSetResolution;
			InParams.Simulating = true;
			InParams.CollisionGroup = Parameters.CollisionGroup;
			InParams.EnableClustering = Parameters.EnableClustering;
			InParams.ClusterGroupIndex = Parameters.ClusterGroupIndex;
			InParams.ClusterConnectionMethod = Parameters.ClusterConnectionMethod;
			Chaos::FErrorReporter ErrorReporter;
			BuildSimulationData(ErrorReporter, *RestCollection, InParams.Shared);
		}

		FParameters Parameters;
		TSharedPtr<FGeometryCollection> RestCollection;
		TSharedPtr<FGeometryDynamicCollection> DynamicCollection;
		TUniquePtr<Chaos::FChaosPhysicsMaterial> PhysicalMaterial;
		TSharedPtr<FGeometryCollectionPhysicsProxy> PhysicsProxy;
		FSharedSimulationSizeSpecificData SimulationData;
	};

}
