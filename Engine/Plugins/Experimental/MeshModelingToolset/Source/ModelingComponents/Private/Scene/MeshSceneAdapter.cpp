// Copyright Epic Games, Inc. All Rights Reserved.


#include "Scene/MeshSceneAdapter.h"

#include "Spatial/MeshAABBTree3.h"
#include "Spatial/FastWinding.h"
#include "MeshDescriptionAdapter.h"

#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "Async/ParallelFor.h"

using namespace UE::Geometry;


class FStaticMeshSpatialWrapper : public IMeshSpatialWrapper
{
public:
	UStaticMesh* StaticMesh = nullptr;
	int32 LODIndex = 0;
	
	FMeshDescription* SourceMesh = nullptr;
	TUniquePtr<FMeshDescriptionTriangleMeshAdapter> Adapter;
	TUniquePtr<TMeshAABBTree3<FMeshDescriptionTriangleMeshAdapter>> AABBTree;
	TUniquePtr<TFastWindingTree<FMeshDescriptionTriangleMeshAdapter>> FWNTree;

	virtual bool Build() override
	{
		check(StaticMesh);
#if WITH_EDITOR
		SourceMesh = StaticMesh->GetMeshDescription(LODIndex);
#else
		checkf(false, TEXT("Not currently supported - to build at Runtime it is necessary to read from the StaticMesh RenderBuffers"));
		SourceMesh = nullptr;
#endif
		if (SourceMesh)
		{
			Adapter = MakeUnique<FMeshDescriptionTriangleMeshAdapter>(SourceMesh);

#if WITH_EDITOR
			// respect BuildScale build setting
			const FMeshBuildSettings& LODBuildSettings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;
			Adapter->SetBuildScale((FVector3d)LODBuildSettings.BuildScale3D, false);
#endif

			AABBTree = MakeUnique<TMeshAABBTree3<FMeshDescriptionTriangleMeshAdapter>>(Adapter.Get(), true);
			FWNTree = MakeUnique<TFastWindingTree<FMeshDescriptionTriangleMeshAdapter>>(AABBTree.Get(), true);
			return true;
		}

		SourceMesh = nullptr;
		return false;
	}


	virtual void CollectSeedPoints(TArray<FVector3d>& WorldPoints, TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		if (!SourceMesh) return;

		int32 NumVertices = Adapter->VertexCount();
		int32 LogNumVertices = FMath::Max(1, (int32)FMathd::Ceil(FMathd::Log(NumVertices)));
		int32 SeedPointCount = (int)(10 * LogNumVertices);
		SeedPointCount = FMath::Min(SeedPointCount, 5000);
		int32 Skip = FMath::Max(NumVertices / SeedPointCount, 2);
		for (int32 k = 0; k < NumVertices; k += Skip)
		{
			WorldPoints.Add(LocalToWorldFunc(Adapter->GetVertex(k)));
		}
	}

	virtual double FastWindingNumber(const FVector3d& P) override
	{
		return (SourceMesh != nullptr) ? FWNTree->FastWindingNumber(P) : 0.0;
	}
};


static TSharedPtr<IMeshSpatialWrapper> SpatialWrapperFactory(const FMeshTypeContainer& MeshContainer)
{
	if (MeshContainer.MeshType == ESceneMeshType::StaticMeshAsset)
	{
		TSharedPtr<FStaticMeshSpatialWrapper> SMWrapper = MakeShared<FStaticMeshSpatialWrapper>();
		SMWrapper->StaticMesh = MeshContainer.GetStaticMesh();
		if (ensure(SMWrapper->StaticMesh != nullptr))
		{
			return SMWrapper;
		}
	}

	return TSharedPtr<IMeshSpatialWrapper>();
}




static void CollectActorChildMeshes(AActor* Actor, UActorComponent* Component, FActorAdapter& Adapter)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (StaticMeshComponent != nullptr)
	{
		UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh();
		if (Mesh != nullptr)
		{
			FActorChildMesh ChildMesh;
			ChildMesh.SourceComponent = Component;
			ChildMesh.MeshContainer = FMeshTypeContainer{ Mesh, ESceneMeshType::StaticMeshAsset };

			UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(StaticMeshComponent);
			if (ISMComponent != nullptr)
			{
				// TODO: HISMC

				ChildMesh.ComponentType = EActorMeshComponentType::InstancedStaticMesh;

				int32 NumInstances = ISMComponent->GetInstanceCount();
				for (int32 i = 0; i < NumInstances; ++i)
				{
					if (ISMComponent->IsValidInstance(i))
					{
						FTransform InstanceTransform;
						if (ensure(ISMComponent->GetInstanceTransform(i, InstanceTransform, true)))
						{
							FActorChildMesh InstanceChild = ChildMesh;
							InstanceChild.ComponentIndex = i;
							InstanceChild.WorldTransform.Append(InstanceTransform);
							InstanceChild.WorldTransformInverse = InstanceChild.WorldTransform.Inverse();
							Adapter.ChildMeshes.Add(InstanceChild);
						}
					}
				}

			}
			else
			{
				// base StaticMeshComponent
				ChildMesh.ComponentType = EActorMeshComponentType::StaticMesh;
				ChildMesh.ComponentIndex = 0;
				ChildMesh.WorldTransform.Append(StaticMeshComponent->GetComponentTransform());
				ChildMesh.WorldTransformInverse = ChildMesh.WorldTransform.Inverse();
				Adapter.ChildMeshes.Add(ChildMesh);
			}
		}
		
	}

}


void FMeshSceneAdapter::AddActors(const TArray<AActor*>& ActorsSetIn)
{
	// build an FActorAdapter for each Actor, that contains all mesh Components we know 
	// how to process, including those contained in ChildActorComponents
	TArray<AActor*> ChildActors;
	for (AActor* Actor : ActorsSetIn)
	{
		TUniquePtr<FActorAdapter> Adapter = MakeUnique<FActorAdapter>();
		Adapter->SourceActor = Actor;

		for (UActorComponent* Component : Actor->GetComponents())
		{
			CollectActorChildMeshes(Actor, Component, *Adapter);
		}

		ChildActors.Reset();
		Actor->GetAllChildActors(ChildActors, true);
		for (AActor* ChildActor : ChildActors)
		{
			for (UActorComponent* Component : ChildActor->GetComponents())
			{
				CollectActorChildMeshes(ChildActor, Component, *Adapter);
			}
		}

		FVector ActorOrigin, ActorExtent;
		Actor->GetActorBounds(false, ActorOrigin, ActorExtent, true);
		Adapter->WorldBounds = FAxisAlignedBox3d(
			(FVector3d)ActorOrigin - (FVector3d)ActorExtent, (FVector3d)ActorOrigin + (FVector3d)ActorExtent);

		SceneActors.Add(MoveTemp(Adapter));
	}


	// Find IMeshSpatialWrapper for each child mesh component. If one does not exist
	// and we have not seen the underlying unique mesh (eg StaticMesh Asset, etc, construct a new one
	TArray<IMeshSpatialWrapper*> NewSpatialsToBuild;
	for (TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (FActorChildMesh& ChildMesh : Actor->ChildMeshes)
		{
			void* MeshKey = ChildMesh.MeshContainer.GetMeshKey();
			TSharedPtr<IMeshSpatialWrapper>* Found = SpatialAdapters.Find(MeshKey);
			if (Found == nullptr)
			{
				TSharedPtr<IMeshSpatialWrapper> NewWrapper = SpatialWrapperFactory(ChildMesh.MeshContainer);
				if (ensure(NewWrapper.IsValid()))
				{
					ChildMesh.MeshSpatial = NewWrapper.Get();
					NewSpatialsToBuild.Add(NewWrapper.Get());
					SpatialAdapters.Add(MeshKey, NewWrapper);
				}
			}
			else
			{
				ChildMesh.MeshSpatial = Found->Get();
			}
		}
	}

	
	// parallel build of all the spatial data structures
	ParallelFor(NewSpatialsToBuild.Num(), [&](int32 i)
	{
		bool bOK = NewSpatialsToBuild[i]->Build();
		ensure(bOK);	// assumption is that the wrapper will handle failure gracefully
	});

}


FAxisAlignedBox3d FMeshSceneAdapter::GetBoundingBox()
{
	FAxisAlignedBox3d SceneBounds = FAxisAlignedBox3d::Empty();
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		SceneBounds.Contain(Actor->WorldBounds);
	}
	return SceneBounds;
}


void FMeshSceneAdapter::CollectMeshSeedPoints(TArray<FVector3d>& Points)
{
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (const FActorChildMesh& ChildMesh : Actor->ChildMeshes)
		{
			if (ChildMesh.MeshSpatial != nullptr)
			{
				ChildMesh.MeshSpatial->CollectSeedPoints(Points,
					[&](const FVector3d& P) { return ChildMesh.WorldTransform.TransformPosition(P); } );
			}
		}
	}
}

double FMeshSceneAdapter::FastWindingNumber(const FVector3d& P)
{
	double SumWinding = 0.0;
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		if (Actor->WorldBounds.Contains(P))
		{
			for (const FActorChildMesh& ChildMesh : Actor->ChildMeshes)
			{
				if (ChildMesh.MeshSpatial != nullptr)
				{
					FVector3d LocalP = ChildMesh.WorldTransformInverse.TransformPosition(P);
					double MeshWinding = ChildMesh.MeshSpatial->FastWindingNumber(LocalP);
					SumWinding += MeshWinding;
				}
			}
		}
	}
	return SumWinding;
}