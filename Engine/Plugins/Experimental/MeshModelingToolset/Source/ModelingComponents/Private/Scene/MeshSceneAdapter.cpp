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
#include "CompGeom/ConvexHull2.h"
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
 * Try to check if a Mesh is "thin", ie basically a planar patch (open or closed).
 * The Normal of the largest-area triangle is taken as the plane normal, and then the "thickness" is measured relative to this plane
 * @param ThinTolerance identify as Thin if the thickness extents is within this size
 * @param ThinPlaneOut thin plane normal will be returned via this frame
 * @param ThicknessOut measured thickness relative to plane will be returned here
 * @return true if mesh is identified as thin under ThinTolerance
 */
template<typename MeshType>
bool IsThinPlanarSubMesh(const MeshType& Mesh, double ThinTolerance, FFrame3d& ThinPlaneOut, double& ThicknessOut)
{
	int32 TriCount = Mesh.TriangleCount();

	// Find triangle with largest area and use it's normal as the plane normal
	// (this is not ideal and we should probably do a normals histogram
	double MaxArea = 0;
	FVector3d MaxAreaNormal;
	FVector3d MaxAreaPoint;
	for (int32 tid = 0; tid < TriCount; ++tid)
	{
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

	// Now compute the bounding box in the local space of this plane
	ThinPlaneOut = FFrame3d(MaxAreaPoint, MaxAreaNormal);
	FAxisAlignedBox3d PlaneExtents = FAxisAlignedBox3d::Empty();
	int32 VertexCount = Mesh.VertexCount();
	for (int32 tid = 0; tid < TriCount; ++tid)
	{
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

		// early-out if we exceed tolerance
		if (PlaneExtents.Depth() > ThinTolerance)
		{
			return false;
		}
	}

	// shift plane to center
	FVector3d Center = PlaneExtents.Center();
	ThinPlaneOut.Origin += Center.X*ThinPlaneOut.X() + Center.Y*ThinPlaneOut.Y() + Center.Z*ThinPlaneOut.Z();
	ThicknessOut = PlaneExtents.Depth();
	return true;
}



/**
 * Try to check if the subset of Triangles of Mesh represent a "thin" region, ie basically a planar patch (open or closed).
 * The Normal of the largest-area triangle is taken as the plane normal, and then the "thickness" is measured relative to this plane
 * @param ThinTolerance identify as Thin if the thickness extents is within this size
 * @param ThinPlaneOut thin plane normal will be returned via this frame
 * @return true if submesh identified as thin
 */
template<typename MeshType>
bool IsThinPlanarSubMesh(const MeshType& Mesh, const TArray<int32>& Triangles, double ThinTolerance, FFrame3d& ThinPlaneOut)
{
	int32 TriCount = Triangles.Num();

	// Find triangle with largest area and use it's normal as the plane normal
	// (this is not ideal and we should probably do a normals histogram
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

	// Now compute the bounding box in the local space of this plane
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

		// early-out if we exceed tolerance
		if (PlaneExtents.Depth() > ThinTolerance)
		{
			return false;
		}
	}

	// shift plane to center
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
	// if true, Mesh is only translated and rotated (allows some assumptions to be made)
	bool bHasBakedScale = false;
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
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_WrapperBuild_DMesh_AABBTree);
				AABBTree = MakeUnique<TMeshAABBTree3<FDynamicMesh3>>(&Mesh, true);
			}
			if (bUseDistanceShellForWinding == false)
			{
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_WrapperBuild_DMesh_FWNTree);
					FWNTree = MakeUnique<TFastWindingTree<FDynamicMesh3>>(AABBTree.Get(), true);
				}
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
			if (bHasBakedTransform || bHasBakedScale)
			{
				FVector3d UseP = (bHasBakedTransform) ? P : LocalToWorldTransform.InverseTransformPosition(P);
				double NearestDistSqr;
				int32 NearTriID = AABBTree->FindNearestTriangle(UseP, NearestDistSqr, IMeshSpatial::FQueryOptions(WindingShellThickness));
				if (NearTriID != IndexConstants::InvalidID)
				{
					// Do we even need to do this? won't we return InvalidID if we don't find point within distance?
					// (also technically we can early-out as soon as we find any point, not the nearest point - might be worth a custom query)
					FDistPoint3Triangle3d Query = TMeshQueries<FDynamicMesh3>::TriangleDistance(Mesh, NearTriID, UseP);
					if (Query.GetSquared() < WindingShellThickness * WindingShellThickness)
					{
						return 1.0;
					}
				}
			}
			else
			{
				ensure(false);		// not supported yet
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
	FVector3d BuildScale = FVector3d::One();
	
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

			BuildScale = FVector3d::One();
#if WITH_EDITOR
			// respect BuildScale build setting
			const FMeshBuildSettings& LODBuildSettings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;
			BuildScale = (FVector3d)LODBuildSettings.BuildScale3D;
			Adapter->SetBuildScale(BuildScale, false);
#endif
			if (BuildOptions.bBuildSpatialDataStructures)
			{
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_WrapperBuild_StaticMesh_AABBTree);
					AABBTree = MakeUnique<TMeshAABBTree3<FMeshDescriptionTriangleMeshAdapter>>(Adapter.Get(), true);
				}
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_WrapperBuild_StaticMesh_FWNTree);
					FWNTree = MakeUnique<TFastWindingTree<FMeshDescriptionTriangleMeshAdapter>>(AABBTree.Get(), true);
				}
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

#if WITH_EDITOR
		if (AppendTo.TriangleCount() == 0 && TransformSeq.Num() == 0)
		{
			// this is somewhat faster in profiling
			FMeshDescription* UseMeshDescription = StaticMesh->GetMeshDescription(LODIndex);
			FMeshDescriptionToDynamicMesh Converter;
			Converter.bEnableOutputGroups = false; Converter.bCalculateMaps = false;
			Converter.bDisableAttributes = true;
			Converter.Convert(UseMeshDescription, AppendTo);
			MeshTransforms::Scale(AppendTo, BuildScale, FVector3d::Zero());
			return;
		}
#endif

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
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_AddActors);

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

}





void FMeshSceneAdapter::Build(const FMeshSceneAdapterBuildOptions& BuildOptions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build);

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
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ActorBounds);
		ParallelFor(SceneActors.Num(), [&](int32 i)
		{
			UpdateActorBounds(*SceneActors[i]);
		});
	}
}


void FMeshSceneAdapter::UpdateActorBounds(FActorAdapter& Actor)
{
	int32 NumChildren = Actor.ChildMeshes.Num();
	TArray<FAxisAlignedBox3d> ChildBounds;
	ChildBounds.Init(FAxisAlignedBox3d::Empty(), NumChildren);
	ParallelFor(NumChildren, [&](int32 k)
	{
		const TUniquePtr<FActorChildMesh>& ChildMesh = Actor.ChildMeshes[k];
		if (ChildMesh->MeshSpatial != nullptr)
		{
			ChildBounds[k] = ChildMesh->MeshSpatial->GetWorldBounds(
				[&](const FVector3d& P) { return ChildMesh->WorldTransform.TransformPosition(P); });
		}
	});

	Actor.WorldBounds = FAxisAlignedBox3d::Empty();
	for (FAxisAlignedBox3d ChildBound : ChildBounds)
	{
		Actor.WorldBounds.Contain(ChildBound);
	}
}



/**
 * This function is used to group the input set of transforms into subsets
 * that have the same scale. Each of those subsets can be represented by
 * a single scaled mesh with different rotate/translate-only transforms.
 * We use this to reduce the number of times a mesh has to be duplicated when
 * breaking it up into parts that require further processing that is incompatible
 * with (nonuniform) scaling.
 * TODO: Currently cannot differentiate between uniform and nonuniform scaling
 */
void ConstructUniqueScalesMapping(
	const TArray<FTransformSequence3d>& TransformSet, 
	TArray<TArray<int32>>& UniqueScaleSetsOut,
	double ScaleComponentTolerance = 0.01)
{
	// two transforms are "the same up to scaling" if this returns true
	auto CompareScales = [ScaleComponentTolerance](const FTransform3d& T1, const FTransform3d& T2)
	{
		return (T1.GetScale() - T2.GetScale()).GetAbsMax() < ScaleComponentTolerance;
	};

	TArray<FTransformSequence3d> UniqueScaleTransforms;		// accumulate transform-with-unique-scale's here
	int32 N = TransformSet.Num();
	TArray<int32> UniqueScaleMap;
	UniqueScaleMap.SetNum(N);
	for (int32 k = 0; k < N; ++k)
	{
		FTransformSequence3d CurTransform = TransformSet[k];
		int32 FoundIndex = -1;
		for (int32 j = 0; j < UniqueScaleTransforms.Num(); ++j)
		{
			if (CurTransform.IsEquivalent(UniqueScaleTransforms[j], CompareScales))
			{
				FoundIndex = j;
				break;
			}
		}
		if (FoundIndex >= 0)
		{
			UniqueScaleMap[k] = FoundIndex;
		}
		else
		{
			UniqueScaleMap[k] = UniqueScaleTransforms.Num();
			UniqueScaleTransforms.Add(CurTransform);
		}
	}

	// build clusters
	int32 NumUniqueScales = UniqueScaleTransforms.Num();
	UniqueScaleSetsOut.SetNum(NumUniqueScales);
	for (int32 k = 0; k < N; ++k)
	{
		UniqueScaleSetsOut[UniqueScaleMap[k]].Add(k);
	}

}



void FMeshSceneAdapter::Build_FullDecompose(const FMeshSceneAdapterBuildOptions& BuildOptions)
{
	// initial list of spatial wrappers that need to be built
	TArray<FSpatialWrapperInfo*> ToBuild;
	for (TPair<void*, TSharedPtr<FSpatialWrapperInfo>> Pair : SpatialAdapters)
	{
		ToBuild.Add(Pair.Value.Get());
	}

	// Initialize the initial set of wrappers. Must do this here so that meshes are loaded and TriangleCount() below is valid
	FMeshSceneAdapterBuildOptions TempBuildOptions = BuildOptions;
	TempBuildOptions.bBuildSpatialDataStructures = false;
	ParallelFor(ToBuild.Num(), [&](int32 i)
	{
		FSpatialWrapperInfo* WrapperInfo = ToBuild[i];
		WrapperInfo->SpatialWrapper->Build(TempBuildOptions);
	});

	// sort build list by increasing triangle count
	ToBuild.Sort([&](const FSpatialWrapperInfo& A, const FSpatialWrapperInfo& B)
	{
		return const_cast<FSpatialWrapperInfo&>(A).SpatialWrapper->GetTriangleCount() < const_cast<FSpatialWrapperInfo&>(B).SpatialWrapper->GetTriangleCount();
	});

	// stats we will collect during execution
	int32 NumInitialSources = ToBuild.Num();
	std::atomic<int32> DecomposedSourceMeshCount = 0;
	std::atomic<int32> DecomposedMeshesCount = 0;
	std::atomic<int32> SourceInstancesCount = 0;
	std::atomic<int32> NewInstancesCount = 0;
	std::atomic<int32> SkippedDecompositionCount = 0;
	int32 AddedUniqueTrisCount = 0;
	int32 InstancedTrisCount = 0;

	// these locks are used below to control access
	FCriticalSection ToBuildQueueLock;
	FCriticalSection InternalListsLock;

	// The loop below will emit new IMeshSpatialWrapper's that need to have Build() called.
	// Since larger meshes take longer, it is a better strategy to collect up these jobs and
	// then call Build() in decreasing-size order
	struct FBuildJob
	{
		int TriangleCount;
		IMeshSpatialWrapper* BuildWrapper;
	};
	TArray<FBuildJob> PendingBuildJobs;
	FCriticalSection PendingBuildsLock;
	// this lambda is used below to append to the PendingBuildJobs list above
	auto AddBuildJob = [&PendingBuildJobs, &PendingBuildsLock](IMeshSpatialWrapper* ToBuild, int TriangleCount)
	{
		PendingBuildsLock.Lock();
		PendingBuildJobs.Add(FBuildJob{ TriangleCount, ToBuild });
		PendingBuildsLock.Unlock();
	};


	// Parallel-process all the ToBuild spatial wrappers. If the mesh is closed and all the pieces are good,
	// this will just emit a Build job. Otherwise it will pull the mesh apart into pieces, move all the closed non-thin
	// pieces into a new instance to be referenced by the original FActorChildMesh, and then make new meshes/wrappers
	// for anything that needs geometric changes (eg to bake in scale, thicken mesh, etc), and in those cases, generate
	// new instances as FActorAdapter/FActorChildMesh's. And emit BuildJob's for those different spatial wrappers.
	ParallelFor(ToBuild.Num(), [&](int32 i)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh);
		
		// ParallelFor will not respect the sorting by triangle-count we did above, so we have to treat the list as a queue and pop from the back
		ToBuildQueueLock.Lock();
		check(ToBuild.Num() > 0);
		FSpatialWrapperInfo* WrapperInfo = ToBuild.Pop(false);
		ToBuildQueueLock.Unlock();

		// get name for debugging purposes
		FString AssetName = WrapperInfo->SourceContainer.GetStaticMesh() ? WrapperInfo->SourceContainer.GetStaticMesh()->GetName() : TEXT("Unknown");

		// convert this mesh to a dynamicmesh for processing
		FDynamicMesh3 LocalMesh;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_1Copy);
			WrapperInfo->SpatialWrapper->AppendMesh(LocalMesh, FTransformSequence3d());
		}

		// should we try to weld here??

		// find separate submeshes of the mesh
		FMeshConnectedComponents Components(&LocalMesh);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_2Components);
			Components.FindConnectedTriangles();
		}
		int32 NumComponents = Components.Num();

		// for each submesh/component, determine if it is closed, and if it is 'thin'
		TArray<bool> IsClosed, IsThin;
		IsClosed.Init(false, NumComponents);
		std::atomic<int32> NumNonClosed = 0;
		IsThin.Init(false, NumComponents);
		std::atomic<int32> NumThin = 0;
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

				FFrame3d TempPlane;
				IsThin[ci] = IsThinPlanarSubMesh<FDynamicMesh3>(LocalMesh, Triangles, BuildOptions.DesiredMinThickness, TempPlane);
				if (IsThin[ci])
				{
					NumThin++;
				}
			});
		}

		// if we have no open meshes and no thin meshes, we can just use the SpatialWrapper we already have, 
		// but we have to rebuild it because we did not do a full build above
		// note: possibly some other cases where we can do this, if the StaticMesh wrapper supported unsigned/offset mode
		if (NumNonClosed == 0 && NumThin == 0)
		{
			AddBuildJob(WrapperInfo->SpatialWrapper.Get(), LocalMesh.TriangleCount());
			return;
		}

		// construct list of per-instance transforms that reference this mesh
		TArray<FActorChildMesh*> MeshesToDecompose = WrapperInfo->ParentMeshes;
		TArray<FTransformSequence3d> ParentTransforms;
		for (FActorChildMesh* MeshInstance : MeshesToDecompose)
		{
			ParentTransforms.Add(MeshInstance->WorldTransform);
			SourceInstancesCount++;
		}

		// Decompose the per-instance transforms into subsets that share the same total scaling ("unique scale").
		// If we apply these different scales to copies of the mesh, we can generate new instances for the copies,
		// which can avoid uniquing a lot of geometry
		TArray<TArray<int32>> UniqueScaleTransformSets;
		ConstructUniqueScalesMapping(ParentTransforms, UniqueScaleTransformSets);
		int32 NumUniqueScales = UniqueScaleTransformSets.Num();

		// Accumulate submesh/components that do *not* need further processing here, that accumulated mesh
		// (if non-empty) can be shared among all the original FActorChildMesh instances
		FDynamicMesh3 LocalSpaceParts;
		FDynamicMeshEditor LocalSpaceAccumulator(&LocalSpaceParts);

		// a new copy of one of the submeshes that has been scaled/processed such that it can only be
		// represented with some of the original instance transforms (NewTransforms). 
		struct FInstancedSubmesh
		{
			TSharedPtr<FDynamicMesh3> Mesh;
			TArray<FTransformSequence3d> NewTransforms;
			double ComputedThickness = 0;
		};
		TArray<FInstancedSubmesh> NewSubmeshes;

		// Split all the submeshes/components into the LocalSpaceParts mesh (for closed and non-thin) and
		// a set of new FInstancedSubmesh's
		{
			FMeshIndexMappings Mappings;		// these are re-used between calls
			FDynamicMeshEditResult EditResult;

			TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_Build_ProcessMesh_4Accumulate);
			for (int32 ci = 0; ci < NumComponents; ++ci)
			{
				const TArray<int32>& Triangles = Components[ci].Indices;

				// We will make unscaled copies of a mesh if (1) it is "thin" and (2) it has a moderate number of triangles *or* a single usage
				// TODO: should we always unique a mesh with a single usage? We can just make it unsigned...
				bool bIsClosed = IsClosed[ci];
				if (IsThin[ci] == false || (Triangles.Num() > 10000 && WrapperInfo->ParentMeshes.Num() > 1))
				{
					Mappings.Reset(); EditResult.Reset();
					LocalSpaceAccumulator.AppendTriangles(&LocalMesh, Triangles, Mappings, EditResult, false);
					continue;
				}

				// if we go this far, we need to unique this mesh once for each "unique scale", and then
				// make a new set of instance transforms for it
				for (int32 k = 0; k < NumUniqueScales; ++k)
				{
					FInstancedSubmesh NewSubmesh;
					const TArray<int32>& InstanceIndices = UniqueScaleTransformSets[k];
					// make unique copy of submesh
					NewSubmesh.Mesh = MakeShared<FDynamicMesh3>();
					FDynamicMeshEditor Editor(NewSubmesh.Mesh.Get());
					Mappings.Reset(); EditResult.Reset();
					Editor.AppendTriangles(&LocalMesh, Triangles, Mappings, EditResult, false);
					// bake in the scaling
					FVector3d Scale = ParentTransforms[InstanceIndices[0]].GetAccumulatedScale();
					for (int32 vid : EditResult.NewVertices)
					{
						FVector3d LocalPos = NewSubmesh.Mesh->GetVertex(vid);
						NewSubmesh.Mesh->SetVertex(vid, LocalPos * Scale);
					}

					// Recompute thickness of scaled mesh and store it. Note that we might fail to be
					// considered "thin" anymore, in that case we will fall back to using winding number
					// for this mesh (So, it was a waste to do this separation, but messy to turn back now)
					FFrame3d TempPlane; double Thickness;
					if (bIsClosed == false)
					{
						NewSubmesh.ComputedThickness = 0;
					}
					else if (bIsClosed && IsThinPlanarSubMesh<FDynamicMesh3>(*NewSubmesh.Mesh, BuildOptions.DesiredMinThickness, TempPlane, Thickness))
					{
						NewSubmesh.ComputedThickness = Thickness;
					}
					else
					{
						NewSubmesh.ComputedThickness = BuildOptions.DesiredMinThickness;
					}

					// make new set of instances
					for (int32 j : InstanceIndices)
					{
						FTransformSequence3d InstanceTransform = ParentTransforms[j];
						InstanceTransform.ClearScales();
						NewSubmesh.NewTransforms.Add(InstanceTransform);
					}
					NewSubmeshes.Add(MoveTemp(NewSubmesh));
				}
			}
		}
		// At this point we have processed all the Submeshes/Components. Now we generate new MeshSpatialWrapper's
		// and any necessary new FActorAdapter's/FActorChildMesh's

		// First handle the LocalSpaceParts mesh, which can still be shared between the original FActorChildMesh instances
		if (LocalSpaceParts.TriangleCount() > 0)
		{
			TUniquePtr<FDynamicMeshSpatialWrapper> LocalSpaceMeshWrapper = MakeUnique<FDynamicMeshSpatialWrapper>();
			LocalSpaceMeshWrapper->Mesh = MoveTemp(LocalSpaceParts);
			AddBuildJob(LocalSpaceMeshWrapper.Get(), LocalSpaceMeshWrapper->Mesh.TriangleCount());
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

		// Exit if we don't have any more work to do. This happens if we ended up skipping all the possible decompositions
		// Note: in this case we could just re-use the existing actor and skip the LocalSpaceParts mesh entirely?
		if (NewSubmeshes.Num() == 0)
		{
			SkippedDecompositionCount++;
			return;
		}

		// definitely decomposing this mesh
		DecomposedSourceMeshCount++;

		// Now we create a new FActorAdapter for each new InstancedSubmesh, and then an FActorChildMesh
		// for each Instance (ie rotate/translate transform of that instance). This is somewhat arbitrary,
		// eg it could all be done in a single Actor, or split up further. At evaluation time we will have
		// pulled these back out of the Actor so it doesn't really matter.
		for (FInstancedSubmesh& Submesh : NewSubmeshes)
		{
			TUniquePtr<FActorAdapter> NewActor = MakeUnique<FActorAdapter>();
			NewActor->SourceActor = nullptr;		// not a "real" actor

			// make new spatialwrapper for this instanced mesh
			TUniquePtr<FDynamicMeshSpatialWrapper> NewInstancedMesh = MakeUnique<FDynamicMeshSpatialWrapper>();
			int32 TriangleCount = Submesh.Mesh->TriangleCount();
			NewInstancedMesh->Mesh = MoveTemp(*Submesh.Mesh);
			NewInstancedMesh->bHasBakedTransform = false;
			NewInstancedMesh->bHasBakedScale = true;
			// queue up build job
			AddBuildJob(NewInstancedMesh.Get(), TriangleCount);
			// if mesh is too thin, configure the extra shell offset based on 'missing' thickness
			if (Submesh.ComputedThickness < BuildOptions.DesiredMinThickness)
			{
				NewInstancedMesh->bUseDistanceShellForWinding = true;
				NewInstancedMesh->WindingShellThickness = 0.5 * (BuildOptions.DesiredMinThickness - Submesh.ComputedThickness);
			}

			TSharedPtr<FSpatialWrapperInfo> NewWrapperInfo = MakeShared<FSpatialWrapperInfo>();
			NewWrapperInfo->SpatialWrapper = MoveTemp(NewInstancedMesh);

			// add to internal lists
			InternalListsLock.Lock();
			AddedUniqueTrisCount += TriangleCount;
			InstancedTrisCount += TriangleCount * Submesh.NewTransforms.Num();
			void* UseKey = (void*)NewWrapperInfo->SpatialWrapper.Get();
			SpatialAdapters.Add(UseKey, NewWrapperInfo);
			InternalListsLock.Unlock();

			// create the new transform instances
			for (FTransformSequence3d InstanceTransform : Submesh.NewTransforms)
			{
				TUniquePtr<FActorChildMesh> ChildMesh = MakeUnique<FActorChildMesh>();
				ChildMesh->SourceComponent = nullptr;
				ChildMesh->ComponentType = EActorMeshComponentType::InternallyGeneratedComponent;
				ChildMesh->ComponentIndex = 0;
				ChildMesh->WorldTransform = InstanceTransform;
				ChildMesh->bIsNonUniformScaled = false;

				NewWrapperInfo->ParentMeshes.Add(ChildMesh.Get());
				ChildMesh->MeshSpatial = NewWrapperInfo->SpatialWrapper.Get();

				NewActor->ChildMeshes.Add(MoveTemp(ChildMesh));
				NewInstancesCount++;
			}

			// add actor our actor set
			InternalListsLock.Lock();
			SceneActors.Add(MoveTemp(NewActor));
			InternalListsLock.Unlock();
		}
	});		// end outer ParallelFor over ToBuild meshes

	check(ToBuild.Num() == 0);

	// Now all that is left is to actually Build() all the different spatial wrappers that exist at this point

	// sort by increasing triangle size. 
	PendingBuildJobs.Sort([](const FBuildJob& A, const FBuildJob& B)
	{
		return A.TriangleCount < B.TriangleCount;
	});
	ParallelFor(PendingBuildJobs.Num(), [&](int32 i)
	{
		// ParallelFor will not respect our sort order if we just use the index directly (because it splits into chunks internally), so
		// we have to treat the list like a queue to get it to be processed in our desired order
		ToBuildQueueLock.Lock();
		check(PendingBuildJobs.Num() > 0);
		FBuildJob BuildJob = PendingBuildJobs.Pop(false);
		ToBuildQueueLock.Unlock();
		BuildJob.BuildWrapper->Build(BuildOptions);
	});
	check(PendingBuildJobs.Num() == 0);

	// currently true with the methods used above?
	bSceneIsAllSolids = true;

	if (BuildOptions.bPrintDebugMessages)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FMeshSceneAdapter] decomposed %d source meshes used in %d instances (of %d total source meshes), into %d new instances containing %ld unique triangles (%ld total instanced). Skipped %d decompositions."), DecomposedSourceMeshCount.load(), SourceInstancesCount.load(), NumInitialSources, NewInstancesCount.load(), AddedUniqueTrisCount, InstancedTrisCount, SkippedDecompositionCount.load())
	}
}




void FMeshSceneAdapter::GetGeometryStatistics(FStatistics& StatsOut)
{
	StatsOut.UniqueMeshCount = 0;
	StatsOut.UniqueMeshTriangleCount = 0;
	for (TPair<void*, TSharedPtr<FSpatialWrapperInfo>> Pair : SpatialAdapters)
	{
		StatsOut.UniqueMeshCount++;
		StatsOut.UniqueMeshTriangleCount += (int64)Pair.Value->SpatialWrapper->GetTriangleCount();
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
				StatsOut.InstanceMeshTriangleCount += (int64)ChildMesh->MeshSpatial->GetTriangleCount();
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
	CachedWorldBounds = FAxisAlignedBox3d::Empty();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_BuildSpatialEvaluationCache_Bounds);
		ParallelFor(SortedSpatials.Num(), [&](int32 k)
		{
			SortedSpatials[k].Bounds = SortedSpatials[k].Spatial->GetWorldBounds(
				[&](const FVector3d& P) { return SortedSpatials[k].ChildMesh->WorldTransform.TransformPosition(P); });
		});

		for (const FSpatialCacheInfo& Cache : SortedSpatials)
		{
			CachedWorldBounds.Contain(Cache.Bounds);
		}
	}


	// build an octree of the mesh objects
	Octree = MakeShared<FSparseDynamicOctree3>();
	Octree->RootDimension = CachedWorldBounds.MaxDim() / 4.0;
	Octree->SetMaxTreeDepth(5);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(MeshScene_BuildSpatialEvaluationCache_OctreeInserts);
		for (int32 k = 0; k < NumSpatials; ++k)
		{
			Octree->InsertObject(k, SortedSpatials[k].Bounds);
		}
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
	DynamicMeshWrapper->bHasBakedTransform = DynamicMeshWrapper->bHasBakedScale = true;
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

