// Copyright Epic Games, Inc. All Rights Reserved.


#include "Scene/MeshSceneAdapter.h"

#include "DynamicMesh3.h"
#include "Spatial/MeshAABBTree3.h"
#include "Spatial/FastWinding.h"
#include "MeshDescriptionAdapter.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshTransforms.h"
#include "BoxTypes.h"
#include "FrameTypes.h"
#include "Selections/MeshConnectedComponents.h"
#include "Operations/OffsetMeshRegion.h"
#include "DynamicMeshEditor.h"
#include "ConvexHull2.h"
#include "Generators/PlanarPolygonMeshGenerator.h"
#include "Spatial/SparseDynamicOctree3.h"

#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "Async/ParallelFor.h"
#include "Async/Async.h"

using namespace UE::Geometry;


/** 
 * Compute the bounds of the vertices of Mesh, under 3D transformation TransformFunc
 * @return computed bounding box
 */
template<typename MeshType>
FAxisAlignedBox3d GetTransformedVertexBounds(const MeshType& Mesh, TFunctionRef<FVector3d(const FVector3d&)> TransformFunc)
{
	FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
	int32 NumVertices = Mesh.VertexCount();
	for (int32 k = 0; k < NumVertices; ++k)
	{
		if (Mesh.IsVertex(k))
		{
			Bounds.Contain(TransformFunc(Mesh.GetVertex(k)));
		}
	}
	return Bounds;
}

/**
 * Collect a subset of vertices of the mesh as "seed points" for algorithms like marching-cubes/etc.
 * Generally every vertex does not need to be used. This function will return at most 5000 point
 * @param TransformFunc transformation applied to points, eg local-to-world mapping
 * @param AccumPointsInOut points are added here
 * @param MaxPoints at most this many vertices will be returned
 */
template<typename MeshType>
void CollectSeedPointsFromMeshVertices(
	const MeshType& Mesh, 
	TFunctionRef<FVector3d(const FVector3d&)> TransformFunc, TArray<FVector3d>& AccumPointsInOut,
	int32 MaxPoints = 500)
{
	int32 NumVertices = Mesh.VertexCount();
	int32 LogNumVertices = FMath::Max(1, (int32)FMathd::Ceil(FMathd::Log(NumVertices)));
	int32 SeedPointCount = (int)(10 * LogNumVertices);
	SeedPointCount = FMath::Min(SeedPointCount, MaxPoints);
	int32 Skip = FMath::Max(NumVertices / SeedPointCount, 2);
	for (int32 k = 0; k < NumVertices; k += Skip)
	{
		AccumPointsInOut.Add(TransformFunc(Mesh.GetVertex(k)));
	}
}


/**
 * Try to check if the subset of Triangles of Mesh represent a "thin" region, ie basically a planar patch (open or closed).
 * The Normal of the largest-area triangle is taken as the plane normal, and then the "thickness" is measured relative to this plain
 * @param ThinTolerance identify as Thin if the thickness extents is within this size
 * @param ThinPlaneOut thin plane normal will be returned via this frame
 * @return true if submesh identified as thin
 */
template<typename MeshType>
bool IsThinPlanarSubMesh(const MeshType& Mesh, const TArray<int32>& Triangles, double ThinTolerance, FFrame3d& ThinPlaneOut)
{
	int32 TriCount = Triangles.Num();
	double MaxArea = 0;
	FVector3d MaxAreaNormal;
	FVector3d MaxAreaPoint;
	for (int32 i = 0; i < TriCount; ++i)
	{
		int32 tid = Triangles[i];
		if (Mesh.IsTriangle(tid))
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(tid, A, B, C);
			double TriArea;
			FVector3d TriNormal = VectorUtil::NormalArea(A, B, C, TriArea);
			if (TriArea > MaxArea)
			{
				MaxArea = TriArea;
				MaxAreaNormal = TriNormal;
				MaxAreaPoint = A;
			}
		}
	}
	ThinPlaneOut = FFrame3d(MaxAreaPoint, MaxAreaNormal);
	FAxisAlignedBox3d PlaneExtents = FAxisAlignedBox3d::Empty();
	int32 VertexCount = Mesh.VertexCount();
	for (int32 i = 0; i < TriCount; ++i)
	{
		int32 tid = Triangles[i];
		if (Mesh.IsTriangle(tid))
		{
			FVector3d TriVerts[3];
			Mesh.GetTriVertices(tid, TriVerts[0], TriVerts[1], TriVerts[2]);
			for (int32 j = 0; j < 3; ++j)
			{
				TriVerts[j] = ThinPlaneOut.ToFramePoint(TriVerts[j]);
				PlaneExtents.Contain(TriVerts[j]);
			}
		}
	}
	if (PlaneExtents.Depth() > ThinTolerance)
	{
		return false;
	}
	FVector3d Center = PlaneExtents.Center();
	ThinPlaneOut.Origin += Center.X*ThinPlaneOut.X() + Center.Y*ThinPlaneOut.Y() + Center.Z*ThinPlaneOut.Z();
	return true;
}



/**
 * @return false if any of Triangles in Mesh have open boundary edges
 */
static bool IsClosedRegion(const FDynamicMesh3& Mesh, const TArray<int32>& Triangles)
{
	for (int32 tid : Triangles)
	{
		FIndex3i TriEdges = Mesh.GetTriEdges(tid);
		if (Mesh.IsBoundaryEdge(TriEdges.A) ||
			Mesh.IsBoundaryEdge(TriEdges.B) ||
			Mesh.IsBoundaryEdge(TriEdges.C))
		{
			return false;
		}
	}
	return true;
}




class FDynamicMeshSpatialWrapper : public IMeshSpatialWrapper
{
public:
	FDynamicMesh3 Mesh;

	// if true, Mesh is in world space (whatever that means)
	bool bHasBakedTransform = false;
	// if true, use unsigned distance to determine inside/outside instead of winding number
	bool bUseDistanceShellForWinding = false;
	// unsigned distance isovalue that defines inside
	double WindingShellThickness = 0.0;

	TUniquePtr<TMeshAABBTree3<FDynamicMesh3>> AABBTree;
	TUniquePtr<TFastWindingTree<FDynamicMesh3>> FWNTree;

	virtual bool Build(const FMeshSceneAdapterBuildOptions& BuildOptions) override
	{
		ensure(Mesh.TriangleCount() > 0);
		if (BuildOptions.bBuildSpatialDataStructures)
		{
			AABBTree = MakeUnique<TMeshAABBTree3<FDynamicMesh3>>(&Mesh, true);
			if (bUseDistanceShellForWinding == false)
			{
				FWNTree = MakeUnique<TFastWindingTree<FDynamicMesh3>>(AABBTree.Get(), true);
			}
		}
		return true;
	}

	virtual int32 GetTriangleCount() override
	{
		return Mesh.TriangleCount();
	}

	virtual FAxisAlignedBox3d GetWorldBounds(TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		FAxisAlignedBox3d Bounds = bHasBakedTransform ?
			GetTransformedVertexBounds<FDynamicMesh3>(Mesh, [&](const FVector3d& P) {return P;}) :
			GetTransformedVertexBounds<FDynamicMesh3>(Mesh, LocalToWorldFunc);
		if (bUseDistanceShellForWinding)
		{
			Bounds.Expand(WindingShellThickness);
		}
		return Bounds;
	}

	virtual void CollectSeedPoints(TArray<FVector3d>& WorldPoints, TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		return bHasBakedTransform ?
			CollectSeedPointsFromMeshVertices<FDynamicMesh3>(Mesh, [&](const FVector3d& P) {return P;}, WorldPoints) :
			CollectSeedPointsFromMeshVertices<FDynamicMesh3>(Mesh, LocalToWorldFunc, WorldPoints);
	}

	virtual double FastWindingNumber(const FVector3d& P, const FTransformSequence3d& LocalToWorldTransform) override
	{
		if (bUseDistanceShellForWinding)
		{
			if (bHasBakedTransform)
			{
				double NearestDistSqr;
				int32 NearTriID = AABBTree->FindNearestTriangle(P, NearestDistSqr, IMeshSpatial::FQueryOptions(WindingShellThickness));
				if (NearTriID != IndexConstants::InvalidID)
				{
					// Do we even need to do this? won't we return InvalidID if we don't find point within distance?
					// (also technically we can early-out as soon as we find any point, not the nearest point - might be worth a custom query)
					FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(Mesh, NearTriID, P);
					if (Query.GetSquared() < WindingShellThickness * WindingShellThickness)
					{
						return 1.0;
					}
				}
			}
			else
			{
				// todo
				check(false);
			}
			return 0.0;
		}
		else
		{
			return bHasBakedTransform ?
				FWNTree->FastWindingNumber(P) :
				FWNTree->FastWindingNumber(LocalToWorldTransform.InverseTransformPosition(P));
		}
	}

	virtual void ProcessVerticesInWorld(TFunctionRef<void(const FVector3d&)> ProcessFunc, const FTransformSequence3d& LocalToWorldTransform) override
	{
		if (bHasBakedTransform)
		{
			for (FVector3d P : Mesh.VerticesItr())
			{
				ProcessFunc(P);
			}
		}
		else
		{
			for (FVector3d P : Mesh.VerticesItr())
			{
				ProcessFunc(LocalToWorldTransform.TransformPosition(P));
			}
		}
	}


	virtual void AppendMesh(FDynamicMesh3& AppendTo, const FTransformSequence3d& TransformSeq) override
	{
		FDynamicMeshEditor Editor(&AppendTo);
		FMeshIndexMappings Mappings;
		if (bHasBakedTransform)
		{
			Editor.AppendMesh(&Mesh, Mappings,
				[&](int, const FVector3d& Pos) { return Pos; },
				[&](int, const FVector3d& Normal) { return Normal; });
		}
		else
		{
			Editor.AppendMesh(&Mesh, Mappings,
				[&](int, const FVector3d& Pos) { return TransformSeq.TransformPosition(Pos); },
				[&](int, const FVector3d& Normal) { return TransformSeq.TransformNormal(Normal); });
		}
	}

};



class FStaticMeshSpatialWrapper : public IMeshSpatialWrapper
{
public:
	UStaticMesh* StaticMesh = nullptr;
	int32 LODIndex = 0;
	
	FMeshDescription* SourceMesh = nullptr;

	TUniquePtr<FMeshDescriptionTriangleMeshAdapter> Adapter;
	TUniquePtr<TMeshAABBTree3<FMeshDescriptionTriangleMeshAdapter>> AABBTree;
	TUniquePtr<TFastWindingTree<FMeshDescriptionTriangleMeshAdapter>> FWNTree;

	virtual bool Build(const FMeshSceneAdapterBuildOptions& BuildOptions) override
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

			FVector3d BuildScale;
#if WITH_EDITOR
			// respect BuildScale build setting
			const FMeshBuildSettings& LODBuildSettings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;
			BuildScale = (FVector3d)LODBuildSettings.BuildScale3D;
			Adapter->SetBuildScale(BuildScale, false);
#endif
			if (BuildOptions.bBuildSpatialDataStructures)
			{
				AABBTree = MakeUnique<TMeshAABBTree3<FMeshDescriptionTriangleMeshAdapter>>(Adapter.Get(), true);
				FWNTree = MakeUnique<TFastWindingTree<FMeshDescriptionTriangleMeshAdapter>>(AABBTree.Get(), true);
			}
			return true;
		}

		SourceMesh = nullptr;
		return false;
	}

	virtual int32 GetTriangleCount() override
	{
		if (!SourceMesh) return 0;
		return Adapter->TriangleCount();
	}


	virtual FAxisAlignedBox3d GetWorldBounds(TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		if (!SourceMesh) return FAxisAlignedBox3d::Empty();
		return GetTransformedVertexBounds<FMeshDescriptionTriangleMeshAdapter>(*Adapter, LocalToWorldFunc);
	}

	virtual void CollectSeedPoints(TArray<FVector3d>& WorldPoints, TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		if (!SourceMesh) return;
		CollectSeedPointsFromMeshVertices<FMeshDescriptionTriangleMeshAdapter>(*Adapter, LocalToWorldFunc, WorldPoints);
	}

	virtual double FastWindingNumber(const FVector3d& P, const FTransformSequence3d& LocalToWorldTransform) override
	{
		return (SourceMesh) ? FWNTree->FastWindingNumber(LocalToWorldTransform.InverseTransformPosition(P)) : 0.0;
	}

	virtual void ProcessVerticesInWorld(TFunctionRef<void(const FVector3d&)> ProcessFunc, const FTransformSequence3d& LocalToWorldTransform) override
	{
		int32 NumVertices = (SourceMesh) ? Adapter->VertexCount() : 0;
		for (int32 vi = 0; vi < NumVertices; ++vi)
		{
			if (Adapter->IsVertex(vi))
			{
				ProcessFunc(LocalToWorldTransform.TransformPosition(Adapter->GetVertex(vi)));
			}
		}
	}

	virtual void AppendMesh(FDynamicMesh3& AppendTo, const FTransformSequence3d& TransformSeq) override
	{
		if (!SourceMesh) return;
		FDynamicMeshEditor Editor(&AppendTo);
		FMeshIndexMappings Mappings;
		FMeshDescriptionMeshAdapterd AdapterWrapper(*Adapter);
		Editor.AppendMesh(&AdapterWrapper, Mappings,
			[&](int, const FVector3d& Pos) { return TransformSeq.TransformPosition(Pos); });
	}

};




static TUniquePtr<IMeshSpatialWrapper> SpatialWrapperFactory( const FMeshTypeContainer& MeshContainer )
{
	if (MeshContainer.MeshType == ESceneMeshType::StaticMeshAsset)
	{
		TUniquePtr<FStaticMeshSpatialWrapper> SMWrapper = MakeUnique<FStaticMeshSpatialWrapper>();
		SMWrapper->SourceContainer = MeshContainer;
		SMWrapper->StaticMesh = MeshContainer.GetStaticMesh();
		if (ensure(SMWrapper->StaticMesh != nullptr))
		{
			return SMWrapper;
		}
	}

	return TUniquePtr<IMeshSpatialWrapper>();
}




static void CollectActorChildMeshes(AActor* Actor, UActorComponent* Component, FActorAdapter& Adapter)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
	if (StaticMeshComponent != nullptr)
	{
		UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh();
		if (Mesh != nullptr)
		{
			TUniquePtr<FActorChildMesh> ChildMesh = MakeUnique<FActorChildMesh>();
			ChildMesh->SourceComponent = Component;
			ChildMesh->MeshContainer = FMeshTypeContainer{ Mesh, ESceneMeshType::StaticMeshAsset };

			UInstancedStaticMeshComponent* ISMComponent = Cast<UInstancedStaticMeshComponent>(StaticMeshComponent);
			if (ISMComponent != nullptr)
			{
				// does anything additional need to happen here for HISMC?

				ChildMesh->ComponentType = EActorMeshComponentType::InstancedStaticMesh;

				int32 NumInstances = ISMComponent->GetInstanceCount();
				for (int32 i = 0; i < NumInstances; ++i)
				{
					if (ISMComponent->IsValidInstance(i))
					{
						FTransform InstanceTransform;
						if (ensure(ISMComponent->GetInstanceTransform(i, InstanceTransform, true)))
						{
							TUniquePtr<FActorChildMesh> InstanceChild = MakeUnique<FActorChildMesh>();
							InstanceChild->SourceComponent = ChildMesh->SourceComponent;
							InstanceChild->MeshContainer = ChildMesh->MeshContainer;
							InstanceChild->ComponentType = ChildMesh->ComponentType;
							InstanceChild->ComponentIndex = i;
							InstanceChild->WorldTransform.Append(InstanceTransform);
							InstanceChild->bIsNonUniformScaled = InstanceChild->WorldTransform.HasNonUniformScale();
							Adapter.ChildMeshes.Add(MoveTemp(InstanceChild));
						}
					}
				}

			}
			else
			{
				// base StaticMeshComponent
				ChildMesh->ComponentType = EActorMeshComponentType::StaticMesh;
				ChildMesh->ComponentIndex = 0;
				ChildMesh->WorldTransform.Append(StaticMeshComponent->GetComponentTransform());
				ChildMesh->bIsNonUniformScaled = ChildMesh->WorldTransform.HasNonUniformScale();
				Adapter.ChildMeshes.Add(MoveTemp(ChildMesh));
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

		SceneActors.Add(MoveTemp(Adapter));
	}


	// Find IMeshSpatialWrapper for each child mesh component. If one does not exist
	// and we have not seen the underlying unique mesh (eg StaticMesh Asset, etc, construct a new one
	for (TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
		{
			void* MeshKey = ChildMesh->MeshContainer.GetMeshKey();
			TSharedPtr<FSpatialWrapperInfo>* Found = SpatialAdapters.Find(MeshKey);
			FSpatialWrapperInfo* MeshInfo = nullptr;
			if (Found == nullptr)
			{
				TSharedPtr<FSpatialWrapperInfo> NewWrapperInfo = MakeShared<FSpatialWrapperInfo>();
				SpatialAdapters.Add(MeshKey, NewWrapperInfo);

				NewWrapperInfo->SourceContainer = ChildMesh->MeshContainer;
				NewWrapperInfo->SpatialWrapper = SpatialWrapperFactory(ChildMesh->MeshContainer);
				MeshInfo = NewWrapperInfo.Get();
			}
			else
			{
				MeshInfo = (*Found).Get();
			}

			MeshInfo->ParentMeshes.Add(ChildMesh.Get());
			if (ChildMesh->bIsNonUniformScaled)
			{
				MeshInfo->NonUniformScaleCount++;
			}
			ChildMesh->MeshSpatial = MeshInfo->SpatialWrapper.Get();
		}
	}


	// TODO: compute per-actor world bounds
	//FVector ActorOrigin, ActorExtent;
	//Actor->GetActorBounds(false, ActorOrigin, ActorExtent, true);
	//Adapter->WorldBounds = FAxisAlignedBox3d(
	//	(FVector3d)ActorOrigin - (FVector3d)ActorExtent, (FVector3d)ActorOrigin + (FVector3d)ActorExtent);
}





void FMeshSceneAdapter::Build(const FMeshSceneAdapterBuildOptions& BuildOptions)
{
	if (BuildOptions.bThickenThinMeshes)
	{
		Build_FullDecompose(BuildOptions);
	}
	else
	{
		TArray<FSpatialWrapperInfo*> ToBuild;
		for (TPair<void*, TSharedPtr<FSpatialWrapperInfo>> Pair : SpatialAdapters)
		{
			ToBuild.Add(Pair.Value.Get());
		}

		FCriticalSection ListsLock;

		std::atomic<int32> DecomposedSourceMeshCount;
		DecomposedSourceMeshCount = 0;
		std::atomic<int32> DecomposedMeshesCount;
		DecomposedMeshesCount = 0;
		int32 AddedTrisCount = 0;

		// parallel build of all the spatial data structures
		ParallelFor(ToBuild.Num(), [&](int32 i)
		{
			FSpatialWrapperInfo* WrapperInfo = ToBuild[i];
			TUniquePtr<IMeshSpatialWrapper>& Wrapper = WrapperInfo->SpatialWrapper;
			bool bOK = Wrapper->Build(BuildOptions);
			ensure(bOK);	// assumption is that the wrapper will handle failure gracefully
		});

		if (BuildOptions.bPrintDebugMessages)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FMeshSceneAdapter] decomposed %d source meshes into %d unique meshes containing %d triangles"), DecomposedSourceMeshCount.load(), DecomposedMeshesCount.load(), AddedTrisCount)
		}

	}


	// update bounding boxes
	ParallelFor(SceneActors.Num(), [&](int32 i)
	{
		UpdateActorBounds(*SceneActors[i]);
	});
}


void FMeshSceneAdapter::UpdateActorBounds(FActorAdapter& Actor)
{
	FAxisAlignedBox3d ActorBounds = FAxisAlignedBox3d::Empty();
	for (const TUniquePtr<FActorChildMesh>& ChildMesh : Actor.ChildMeshes)
	{
		if (ChildMesh->MeshSpatial != nullptr)
		{
			FAxisAlignedBox3d ChildBounds = ChildMesh->MeshSpatial->GetWorldBounds(
				[&](const FVector3d& P) { return ChildMesh->WorldTransform.TransformPosition(P); });
			ActorBounds.Contain(ChildBounds);
		}
	}
	Actor.WorldBounds = ActorBounds;
}


void FMeshSceneAdapter::Build_FullDecompose(const FMeshSceneAdapterBuildOptions& BuildOptions)
{
	TArray<FSpatialWrapperInfo*> ToBuild;
	for (TPair<void*, TSharedPtr<FSpatialWrapperInfo>> Pair : SpatialAdapters)
	{
		ToBuild.Add(Pair.Value.Get());
	}

	FCriticalSection ListsLock;

	std::atomic<int32> DecomposedSourceMeshCount;
	DecomposedSourceMeshCount = 0;
	std::atomic<int32> DecomposedMeshesCount;
	DecomposedMeshesCount = 0;
	int32 AddedTrisCount = 0;

	FCriticalSection PendingFuturesLock;
	TArray<TFuture<void>> PendingClosedAssemblyBuilds;

	// use this to disable build of unnecessary AABBTree/etc
	FMeshSceneAdapterBuildOptions TempBuildOptions = BuildOptions;
	TempBuildOptions.bBuildSpatialDataStructures = false;

	// parallel build of all the spatial data structures
	ParallelFor(ToBuild.Num(), [&](int32 i)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh);

		FSpatialWrapperInfo* WrapperInfo = ToBuild[i];

		// convert this mesh to a dynamicmesh for processing
		FDynamicMesh3 LocalMesh;
		WrapperInfo->SpatialWrapper->Build(TempBuildOptions);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_1Copy);
			WrapperInfo->SpatialWrapper->AppendMesh(LocalMesh, FTransformSequence3d());
		}

		// should we try to weld here??

		FMeshConnectedComponents Components(&LocalMesh);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_2Components);
			Components.FindConnectedTriangles();
		}
		int32 NumComponents = Components.Num();
		TArray<bool> IsClosed;
		IsClosed.Init(false, NumComponents);
		std::atomic<int32> NumNonClosed = 0;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_3Closed);
			ParallelFor(NumComponents, [&](int32 ci)
			{
				const FMeshConnectedComponents::FComponent& Component = Components[ci];
				const TArray<int32>& Triangles = Component.Indices;
				IsClosed[ci] = IsClosedRegion(LocalMesh, Triangles);
				if (IsClosed[ci] == false)
				{
					NumNonClosed++;
				}
			});
		}

		// if we have no open meshes, we can just use the SpatialWrapper we already have, but we have to
		// rebuild it because we did not do a full build above
		if (NumNonClosed == 0)
		{
			WrapperInfo->SpatialWrapper->Build(BuildOptions);
			return;
		}

		// NOTE: because of thin-planar checks below, we might not actually decompose this mesh!!
		DecomposedSourceMeshCount++;

		// list of per-instance transforms
		TArray<FActorChildMesh*> MeshesToDecompose = WrapperInfo->ParentMeshes;
		TArray<FTransformSequence3d> ParentTransforms;
		for (FActorChildMesh* MeshInstance : MeshesToDecompose)
		{
			ParentTransforms.Add(MeshInstance->WorldTransform);
		}

		bool bMakeClosedAssembly = (NumNonClosed != NumComponents);
		FDynamicMesh3 LocalSpaceParts;
		FDynamicMeshEditor LocalSpaceAccumulator(&LocalSpaceParts);
		FDynamicMesh3 WorldSpaceParts;
		FDynamicMeshEditor WorldSpaceAccumulator(&WorldSpaceParts);

		int32 LocalCount = 0, DecomposedCount = 0;

		// Accumulate closed parts in a local-space mesh that can stay instanced, and open parts
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_4Accumulate);
			for (int32 ci = 0; ci < NumComponents; ++ci)
			{
				const TArray<int32>& Triangles = Components[ci].Indices;

				bool bKeepLocal = true;
				FFrame3d ThinMeshPlane;
				if (IsClosed[ci] == false)
				{
					bool IsThinMesh = IsThinPlanarSubMesh<FDynamicMesh3>(LocalMesh, Triangles, BuildOptions.DesiredMinThickness, ThinMeshPlane);
					if (IsThinMesh && (Triangles.Num() < 10000 || WrapperInfo->ParentMeshes.Num() == 1))
					{
						bKeepLocal = false;
					}
				}

				FMeshIndexMappings Mappings;
				FDynamicMeshEditResult EditResult;
				if (bKeepLocal)
				{
					LocalSpaceAccumulator.AppendTriangles(&LocalMesh, Triangles, Mappings, EditResult, false);
					LocalCount++;
				}
				else
				{
					DecomposedCount++;
					for (const FTransformSequence3d& Transform : ParentTransforms)
					{
						DecomposedMeshesCount++;
						Mappings.Reset();
						EditResult.Reset();
						WorldSpaceAccumulator.AppendTriangles(&LocalMesh, Triangles, Mappings, EditResult, false);
						for (int32 vid : EditResult.NewVertices)
						{
							FVector3d LocalPos = WorldSpaceParts.GetVertex(vid);
							WorldSpaceParts.SetVertex(vid, Transform.TransformPosition(LocalPos));
						}
					}
				}
			}
		}

		// make new mesh containers

		// we re-use the existing wrapper for the closed meshes, if there are any, otherwise
		// we need to disable the parent meshes that refer to it
		if (bMakeClosedAssembly)
		{
			TUniquePtr<FDynamicMeshSpatialWrapper> LocalSpaceMeshWrapper = MakeUnique<FDynamicMeshSpatialWrapper>();
			
			LocalSpaceMeshWrapper->Mesh = MoveTemp(LocalSpaceParts);
			FDynamicMeshSpatialWrapper* BuildWrapper = LocalSpaceMeshWrapper.Get();
			TFuture<void> PendingClosedAssemblyBuild = Async(EAsyncExecution::ThreadPool, [&BuildOptions, BuildWrapper]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_6SolidBuild);
				BuildWrapper->Build(BuildOptions);
			});

			PendingFuturesLock.Lock();
			PendingClosedAssemblyBuilds.Add(MoveTemp(PendingClosedAssemblyBuild));
			PendingFuturesLock.Unlock();

			WrapperInfo->SpatialWrapper = MoveTemp(LocalSpaceMeshWrapper);

			for (FActorChildMesh* MeshInstance : MeshesToDecompose)
			{
				MeshInstance->MeshSpatial = WrapperInfo->SpatialWrapper.Get();
			}
		}
		else
		{
			// have to null out spatials for the child meshes so that they are ignored during computation
			for (FActorChildMesh* MeshInstance : MeshesToDecompose)
			{
				MeshInstance->MeshSpatial = nullptr;
			}
		}

		// currently happens if we fail to find open parts to expand/etc
		if (WorldSpaceParts.TriangleCount() == 0)
		{
			return;
		}

		// world-space meshes are added to new fake adapter
		TUniquePtr<FActorAdapter> Adapter = MakeUnique<FActorAdapter>();
		Adapter->SourceActor = nullptr;

		TUniquePtr<FActorChildMesh> ChildMesh = MakeUnique<FActorChildMesh>();
		ChildMesh->SourceComponent = nullptr;
		ChildMesh->ComponentType = EActorMeshComponentType::InternallyGeneratedComponent;
		ChildMesh->ComponentIndex = 0;
		ChildMesh->bIsNonUniformScaled = false;


		TUniquePtr<FDynamicMeshSpatialWrapper> NewWorldSpaceSpatial = MakeUnique<FDynamicMeshSpatialWrapper>();
		NewWorldSpaceSpatial->Mesh = MoveTemp(WorldSpaceParts);
		NewWorldSpaceSpatial->bHasBakedTransform = true;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_8WorldSpaceBuild);
			NewWorldSpaceSpatial->Build(BuildOptions);
		}
		NewWorldSpaceSpatial->bUseDistanceShellForWinding = true;
		NewWorldSpaceSpatial->WindingShellThickness = 0.5 * BuildOptions.DesiredMinThickness;

		TSharedPtr<FSpatialWrapperInfo> NewWrapperInfo = MakeShared<FSpatialWrapperInfo>();
		NewWrapperInfo->SpatialWrapper = MoveTemp(NewWorldSpaceSpatial);
		NewWrapperInfo->ParentMeshes.Add(ChildMesh.Get());
		ChildMesh->MeshSpatial = NewWrapperInfo->SpatialWrapper.Get();

		void* UseKey = ChildMesh.Get();
		Adapter->ChildMeshes.Add(MoveTemp(ChildMesh));

		ListsLock.Lock();
		SpatialAdapters.Add(UseKey, NewWrapperInfo);
		SceneActors.Add(MoveTemp(Adapter));
		AddedTrisCount += NewWrapperInfo->SpatialWrapper->GetTriangleCount();
		ListsLock.Unlock();
	});

	// make sure these finishes
	for (auto& PendingClosedAssemblyBuild : PendingClosedAssemblyBuilds)
	{
		PendingClosedAssemblyBuild.Wait();
	}	

	// currently true with the methods used above?
	bSceneIsAllSolids = true;

	if (BuildOptions.bPrintDebugMessages)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FMeshSceneAdapter] decomposed %d source meshes into %d unique meshes containing %d triangles"), DecomposedSourceMeshCount.load(), DecomposedMeshesCount.load(), AddedTrisCount)
	}
}




void FMeshSceneAdapter::GetGeometryStatistics(FStatistics& StatsOut)
{
	StatsOut.UniqueMeshCount = 0;
	StatsOut.UniqueMeshTriangleCount = 0;
	for (TPair<void*, TSharedPtr<FSpatialWrapperInfo>> Pair : SpatialAdapters)
	{
		StatsOut.UniqueMeshCount++;
		StatsOut.UniqueMeshTriangleCount += Pair.Value->SpatialWrapper->GetTriangleCount();
	}

	StatsOut.InstanceMeshCount = 0;
	StatsOut.InstanceMeshTriangleCount = 0;
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (const TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
		{
			StatsOut.InstanceMeshCount++;
			if (ChildMesh->MeshSpatial != nullptr)
			{
				StatsOut.InstanceMeshTriangleCount += ChildMesh->MeshSpatial->GetTriangleCount();
			}
		}
	}
}


FAxisAlignedBox3d FMeshSceneAdapter::GetBoundingBox()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_GetBoundingBox);

	if (bHaveSpatialEvaluationCache)
	{
		return CachedWorldBounds;
	}

	// this could be done in parallel...
	FAxisAlignedBox3d SceneBounds = FAxisAlignedBox3d::Empty();
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (const TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
		{
			if (ChildMesh->MeshSpatial != nullptr)
			{
				FAxisAlignedBox3d ChildBounds = ChildMesh->MeshSpatial->GetWorldBounds(
					[&](const FVector3d& P) { return ChildMesh->WorldTransform.TransformPosition(P); });
				SceneBounds.Contain(ChildBounds);
			}
		}
	}
	return SceneBounds;
}


void FMeshSceneAdapter::CollectMeshSeedPoints(TArray<FVector3d>& Points)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_CollectMeshSeedPoints);

	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (const TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
		{
			if (ChildMesh->MeshSpatial != nullptr)
			{
				ChildMesh->MeshSpatial->CollectSeedPoints(Points,
					[&](const FVector3d& P) { return ChildMesh->WorldTransform.TransformPosition(P); } );
			}
		}
	}
}

double FMeshSceneAdapter::FastWindingNumber(const FVector3d& P, bool bFastEarlyOutIfPossible)
{
	check(bHaveSpatialEvaluationCache);		// must call BuildSpatialEvaluationCache() to build Octree

	double SumWinding = 0.0;

	// if all objects in scene are solids, then all winding queries will return integers so if any value
	// is > 0, we are "inside"
	if (bSceneIsAllSolids)
	{
		if (bFastEarlyOutIfPossible)
		{
			bool bFinished = Octree->ContainmentQueryCancellable(P, [&](int32 k)
			{
				double WindingNumber = SortedSpatials[k].Spatial->FastWindingNumber(P, SortedSpatials[k].ChildMesh->WorldTransform);
				SumWinding += WindingNumber;
				return (FMath::Abs(WindingNumber) < 0.99);		// if we see an "inside" winding number we can just exit
			});
		}
		else
		{
			Octree->ContainmentQuery(P, [&](int32 k)
			{
				double WindingNumber = SortedSpatials[k].Spatial->FastWindingNumber(P, SortedSpatials[k].ChildMesh->WorldTransform);
				SumWinding += WindingNumber;
			});
		}
	}
	else
	{
		for (const FSpatialCacheInfo& SpatialInfo : SortedSpatials)
		{
			double WindingNumber = SpatialInfo.Spatial->FastWindingNumber(P, SpatialInfo.ChildMesh->WorldTransform);
			SumWinding += WindingNumber;
		}
	}

	return SumWinding;
}



void FMeshSceneAdapter::BuildSpatialEvaluationCache()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_BuildSpatialEvaluationCache);

	// build list of unique meshes we need to evaluate for spatial queries
	SortedSpatials.Reset();
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (const TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
		{
			if (ChildMesh->MeshSpatial != nullptr)
			{
				FSpatialCacheInfo Cache;
				Cache.Actor = Actor.Get();
				Cache.ChildMesh = ChildMesh.Get();
				Cache.Spatial = ChildMesh->MeshSpatial;
				Cache.Bounds = ChildMesh->MeshSpatial->GetWorldBounds(
					[&](const FVector3d& P) { return ChildMesh->WorldTransform.TransformPosition(P); });
				SortedSpatials.Add(Cache);


			}
		}
	}

	// sort the list (not really necessary but might improve cache coherency during linear queries)
	SortedSpatials.Sort([&](const FSpatialCacheInfo& A, const FSpatialCacheInfo& B)
	{
		return A.Spatial < B.Spatial;
	});

	int32 NumSpatials = SortedSpatials.Num();
	ParallelFor(SortedSpatials.Num(), [&](int32 k)
	{
		SortedSpatials[k].Bounds = SortedSpatials[k].Spatial->GetWorldBounds(
			[&](const FVector3d& P) { return SortedSpatials[k].ChildMesh->WorldTransform.TransformPosition(P); });
	});

	CachedWorldBounds = FAxisAlignedBox3d::Empty();
	for (const FSpatialCacheInfo& Cache : SortedSpatials )
	{
		CachedWorldBounds.Contain(Cache.Bounds);
	}

	// build an octree of the mesh objects
	Octree = MakeShared<FSparseDynamicOctree3>();
	Octree->RootDimension = CachedWorldBounds.MaxDim() / 4.0;
	Octree->SetMaxTreeDepth(5);
	for (int32 k = 0; k < NumSpatials; ++k)
	{
		Octree->InsertObject(k, SortedSpatials[k].Bounds);
	}

	bHaveSpatialEvaluationCache = true;
}



void FMeshSceneAdapter::GetAccumulatedMesh(FDynamicMesh3& AccumMesh)
{
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (const TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
		{
			if (ChildMesh->MeshSpatial != nullptr)
			{
				ChildMesh->MeshSpatial->AppendMesh(AccumMesh, ChildMesh->WorldTransform);
			}
		}
	}
}




void FMeshSceneAdapter::GenerateBaseClosingMesh(double BaseHeight, double ExtrudeHeight)
{
	FAxisAlignedBox3d WorldBounds = GetBoundingBox();
	FInterval1d ZRange(WorldBounds.Min.Z, WorldBounds.Min.Z + BaseHeight);

	TArray<FActorChildMesh*> AllChildMeshes;
	for (TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		for (TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
		{
			if (ChildMesh->MeshSpatial != nullptr)
			{
				AllChildMeshes.Add(ChildMesh.Get());
			}
		}
	}

	TArray<FVector2d> WorldHullPoints;
	FCriticalSection WorldHullPointsLock;

	int32 NumChildren = AllChildMeshes.Num();
	ParallelFor(NumChildren, [&](int32 ci)
	{
		FActorChildMesh* ChildMesh = AllChildMeshes[ci];
		TArray<FVector2d> LocalHullPoints;
		ChildMesh->MeshSpatial->ProcessVerticesInWorld([&](const FVector3d& WorldPos)
		{
			if (ZRange.Contains(WorldPos.Z))
			{
				LocalHullPoints.Add(FVector2d(WorldPos.X, WorldPos.Y));
			}
		}, ChildMesh->WorldTransform);

		if (LocalHullPoints.Num() > 0)
		{
			FConvexHull2d HullSolver;
			if (HullSolver.Solve(LocalHullPoints))
			{
				WorldHullPointsLock.Lock();
				for (int32 idx : HullSolver.GetPolygonIndices())
				{
					WorldHullPoints.Add(LocalHullPoints[idx]);
				}
				WorldHullPointsLock.Unlock();
			}
		}
	});

	FConvexHull2d FinalHullSolver;
	bool bOK = FinalHullSolver.Solve(WorldHullPoints);
	if (bOK == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FMeshSceneAdapter::GenerateBaseClosingMesh] failed to solve convex hull"));
		return;
	}
	FPolygon2d ConvexHullPoly;
	for (int32 idx : FinalHullSolver.GetPolygonIndices())
	{
		ConvexHullPoly.AppendVertex(WorldHullPoints[idx]);
	}
	if (ConvexHullPoly.VertexCount() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FMeshSceneAdapter::GenerateBaseClosingMesh] convex hull is degenerate"));
		return;
	}

	FPlanarPolygonMeshGenerator MeshGen;
	MeshGen.Polygon = ConvexHullPoly;
	FDynamicMesh3 BasePolygonMesh(&MeshGen.Generate());
	MeshTransforms::Translate(BasePolygonMesh, ZRange.Min * FVector3d::UnitZ());

	if (ExtrudeHeight == 0)
	{
		BasePolygonMesh.ReverseOrientation();		// flip so it points down
		bSceneIsAllSolids = false;					// if scene was solids, it's not anymore
	}
	else
	{
		FOffsetMeshRegion Offset(&BasePolygonMesh);
		for (int32 tid : BasePolygonMesh.TriangleIndicesItr())
		{
			Offset.Triangles.Add(tid);
		}
		Offset.bUseFaceNormals = true;
		Offset.DefaultOffsetDistance = ExtrudeHeight;
		Offset.bIsPositiveOffset = (ExtrudeHeight > 0);
		Offset.Apply();
	}

	//
	// append a fake actor/mesh
	//

	TUniquePtr<FActorAdapter> ActorAdapter = MakeUnique<FActorAdapter>();
	ActorAdapter->SourceActor = nullptr;

	TUniquePtr<FActorChildMesh> ChildMesh = MakeUnique<FActorChildMesh>();
	ChildMesh->SourceComponent = nullptr;
	//InstanceChild->MeshContainer = ;
	ChildMesh->ComponentType = EActorMeshComponentType::InternallyGeneratedComponent;
	ChildMesh->ComponentIndex = 0;
	//ChildMesh->WorldTransform.Append(InstanceTransform);
	ChildMesh->bIsNonUniformScaled = false;

	TSharedPtr<FSpatialWrapperInfo> NewWrapperInfo = MakeShared<FSpatialWrapperInfo>();
	SpatialAdapters.Add(ChildMesh.Get(), NewWrapperInfo);
	TUniquePtr<FDynamicMeshSpatialWrapper> DynamicMeshWrapper = MakeUnique<FDynamicMeshSpatialWrapper>();
	//DynamicMeshWrapper->SourceContainer = NewWrapperInfo->SourceContainer;
	DynamicMeshWrapper->Mesh = MoveTemp(BasePolygonMesh);
	DynamicMeshWrapper->bHasBakedTransform = true;
	FMeshSceneAdapterBuildOptions UseBuildOptions;
	DynamicMeshWrapper->Build(UseBuildOptions);

	//NewWrapperInfo->SourceContainer = ChildMesh->MeshContainer;
	NewWrapperInfo->SpatialWrapper = MoveTemp(DynamicMeshWrapper);
	NewWrapperInfo->ParentMeshes.Add(ChildMesh.Get());
	ChildMesh->MeshSpatial = NewWrapperInfo->SpatialWrapper.Get();
	ActorAdapter->ChildMeshes.Add(MoveTemp(ChildMesh));
	UpdateActorBounds(*ActorAdapter);
	SceneActors.Add(MoveTemp(ActorAdapter));

}

