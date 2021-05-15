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

#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "Async/ParallelFor.h"

using namespace UE::Geometry;


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


template<typename MeshType>
void CollectSeedPointsFromMeshVertices(const MeshType& Mesh, TFunctionRef<FVector3d(const FVector3d&)> TransformFunc, TArray<FVector3d>& AccumPoints)
{
	int32 NumVertices = Mesh.VertexCount();
	int32 LogNumVertices = FMath::Max(1, (int32)FMathd::Ceil(FMathd::Log(NumVertices)));
	int32 SeedPointCount = (int)(10 * LogNumVertices);
	SeedPointCount = FMath::Min(SeedPointCount, 5000);
	int32 Skip = FMath::Max(NumVertices / SeedPointCount, 2);
	for (int32 k = 0; k < NumVertices; k += Skip)
	{
		AccumPoints.Add(TransformFunc(Mesh.GetVertex(k)));
	}
}

template<typename MeshType>
FDynamicMesh3 FastConvertToDynamicMesh(const MeshType& Mesh)
{
	FDynamicMesh3 NewMesh;
	int32 VertexCount = Mesh.VertexCount();
	TArray<int32> VertexIDMap;
	VertexIDMap.SetNum(VertexCount);
	for (int32 vid = 0; vid < VertexCount; ++vid)
	{
		if (Mesh.IsVertex(vid))
		{
			int32 newvid = NewMesh.AppendVertex(Mesh.GetVertex(vid));
			VertexIDMap[vid] = newvid;
		}
	}

	int32 TriCount = Mesh.TriangleCount();
	for (int32 tid = 0; tid < TriCount; ++tid)
	{
		if (Mesh.IsTriangle(tid))
		{
			FIndex3i TriVerts = Mesh.GetTriangle(tid);
			NewMesh.AppendTriangle(VertexIDMap[TriVerts.A], VertexIDMap[TriVerts.B], VertexIDMap[TriVerts.C]);
		}
	}

	return NewMesh;
}


template<typename MeshType>
bool IsThinPlanarMesh(const MeshType& Mesh, double ThinTolerance, FFrame3d& ThinPlane)
{
	int32 TriCount = Mesh.TriangleCount();
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
	ThinPlane = FFrame3d(MaxAreaPoint, MaxAreaNormal);
	FAxisAlignedBox3d PlaneExtents = FAxisAlignedBox3d::Empty();
	int32 VertexCount = Mesh.VertexCount();
	for (int32 vid = 0; vid < VertexCount; ++vid)
	{
		if (Mesh.IsVertex(vid))
		{
			FVector3d Pos = Mesh.GetVertex(vid);
			Pos = ThinPlane.ToFramePoint(Pos);
			PlaneExtents.Contain(Pos);
		}
	}
	if (PlaneExtents.Depth() > ThinTolerance)
	{
		return false;
	}
	FVector3d Center = PlaneExtents.Center();
	ThinPlane.Origin += Center.X*ThinPlane.X() + Center.Y*ThinPlane.Y() + Center.Z*ThinPlane.Z();
	return true;
}


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


template<typename Func>
void ApplyToTriangleVertexIndices(const FDynamicMesh3& Mesh, const TArray<int32>& Triangles, Func VertexFunc)
{
	for (int32 tid : Triangles)
	{
		FIndex3i TriVerts = Mesh.GetTriangle(tid);
		VertexFunc(TriVerts.A);
		VertexFunc(TriVerts.B);
		VertexFunc(TriVerts.C);
	}
}

template<typename Func>
void ApplyToTriangleVertices(const FDynamicMesh3& Mesh, const TArray<int32>& Triangles, Func VertexFunc)
{
	FVector3d A, B, C;
	for (int32 tid : Triangles)
	{
		Mesh.GetTriVertices(tid, A, B, C);
		VertexFunc(A);
		VertexFunc(B);
		VertexFunc(C);
	}
}

static void ThickenThinPlanarMesh(FDynamicMesh3& Mesh, double TargetThickness, const FFrame3d& ThinPlane)
{
	FVector3d PlaneZ = ThinPlane.Z();

	TArray<bool> VertFlags;
	VertFlags.Init(false, Mesh.VertexCount());

	FMeshConnectedComponents Components(&Mesh);
	Components.FindConnectedTriangles();
	int32 NumComponents = Components.Num();
	for (const FMeshConnectedComponents::FComponent& Component : Components)
	{
		const TArray<int32>& Triangles = Component.Indices;
		bool bIsClosed = IsClosedRegion(Mesh, Triangles);

		if (bIsClosed)
		{
			FInterval1d ThicknessRange = FInterval1d::Empty();
			ApplyToTriangleVertices(Mesh, Triangles, [&](const FVector3d& V) {
				ThicknessRange.Contain((V - ThinPlane.Origin).Dot(PlaneZ));
			});
			double CurThickness = ThicknessRange.Length();

			double MoveDelta = (TargetThickness - CurThickness) * 0.5;

			int32 NumTriangles = Triangles.Num();
			TArray<FVector3d> TriNormals;
			TriNormals.SetNum(NumTriangles);
			for (int32 k = 0; k < NumTriangles; ++k)
			{
				TriNormals[k] = Mesh.GetTriNormal(Triangles[k]);
			}

			//for (int32 k = 0; k < NumTriangles; ++k)
			//{
			//	int32 tid = Triangles[k];
			//	FIndex3i TriVerts = Mesh.GetTriangle(tid);
			//	FVector3d MoveDir = TriNormals[k].Dot(PlaneZ) > 0 ? PlaneZ : -PlaneZ;
			//	for (int32 j = 0; j < 3; ++j)
			//	{
			//		int32 vid = TriVerts[j];
			//		if (VertFlags[vid] == false)
			//		{
			//			Mesh.SetVertex(vid, Mesh.GetVertex(vid) + MoveDir * MoveDelta);
			//			VertFlags[vid] = true;
			//		}
			//	}
			//}

		}
		else   // bIsClosed == false
		{
			FOffsetMeshRegion Offset(&Mesh);
			Offset.Triangles = Triangles;
			//Offset.bUseFaceNormals = true;
			Offset.DefaultOffsetDistance = -TargetThickness;
			Offset.bIsPositiveOffset = false;
			Offset.Apply();
		}
	}
}





static bool CheckIfStaticMeshUsageRequiresDynamicMeshDecomposition(
	UStaticMesh* StaticMesh, 
	int32 LODIndex,
	const FMeshSceneAdapterBuildOptions& BuildOptions,
	const TArray<FActorChildMesh*>& ParentMeshes,
	int32 NonUniformScaleCount )
{
	FMeshDescription* SourceMesh;
	check(StaticMesh);
#if WITH_EDITOR
	SourceMesh = StaticMesh->GetMeshDescription(LODIndex);
#else
	checkf(false, TEXT("Not currently supported - to build at Runtime it is necessary to read from the StaticMesh RenderBuffers"));
	return false;
#endif

	if (SourceMesh)
	{
		// does not need this - copypasta
		FMeshDescriptionTriangleMeshAdapter Adapter(SourceMesh);

		FVector3d BuildScale;
#if WITH_EDITOR
		// respect BuildScale build setting
		const FMeshBuildSettings& LODBuildSettings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;
		Adapter.SetBuildScale((FVector3d)LODBuildSettings.BuildScale3D, false);
#endif
		if (BuildOptions.bThickenThinMeshes)
		{
			FFrame3d ThinMeshPlane;
			bool IsThinMesh = IsThinPlanarMesh<FMeshDescriptionTriangleMeshAdapter>(Adapter, BuildOptions.DesiredMinThickness, ThinMeshPlane);
			if (IsThinMesh)
			{
				if (Adapter.TriangleCount() > 100000 && ParentMeshes.Num() > 1)
				{
					return false;		// skip thickening of huge meshes that are frequently used
				}

				return true;
			}
		}
	}
	return false;
}





class FDynamicMeshSpatialWrapper : public IMeshSpatialWrapper
{
public:
	FDynamicMesh3 Mesh;
	bool bHasBakedTransform = false;
	TUniquePtr<TMeshAABBTree3<FDynamicMesh3>> AABBTree;
	TUniquePtr<TFastWindingTree<FDynamicMesh3>> FWNTree;

	virtual bool Build(const FMeshSceneAdapterBuildOptions& BuildOptions) override
	{
		ensure(Mesh.TriangleCount() > 0);
		AABBTree = MakeUnique<TMeshAABBTree3<FDynamicMesh3>>(&Mesh, true);
		FWNTree = MakeUnique<TFastWindingTree<FDynamicMesh3>>(AABBTree.Get(), true);
		return true;
	}

	virtual int32 GetTriangleCount() override
	{
		return Mesh.TriangleCount();
	}

	virtual FAxisAlignedBox3d GetWorldBounds(TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		return bHasBakedTransform ?
			GetTransformedVertexBounds<FDynamicMesh3>(Mesh, [&](const FVector3d& P) {return P;}) :
			GetTransformedVertexBounds<FDynamicMesh3>(Mesh, LocalToWorldFunc);
	}

	virtual void CollectSeedPoints(TArray<FVector3d>& WorldPoints, TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		return bHasBakedTransform ?
			CollectSeedPointsFromMeshVertices<FDynamicMesh3>(Mesh, [&](const FVector3d& P) {return P;}, WorldPoints) :
			CollectSeedPointsFromMeshVertices<FDynamicMesh3>(Mesh, LocalToWorldFunc, WorldPoints);
	}

	virtual double FastWindingNumber(const FVector3d& P, const FTransformSequence3d& LocalToWorldTransform) override
	{
		return bHasBakedTransform ?
			FWNTree->FastWindingNumber(P) :
			FWNTree->FastWindingNumber(LocalToWorldTransform.InverseTransformPosition(P));
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






class FStaticMeshDynamicSpatialWrapper : public IMeshSpatialWrapper
{
public:
	UStaticMesh* StaticMesh = nullptr;
	int32 LODIndex = 0;
	
	FMeshDescription* SourceMesh = nullptr;

	TUniquePtr<FDynamicMeshSpatialWrapper> DynamicWrapper;

	virtual bool Build(const FMeshSceneAdapterBuildOptions& BuildOptions) override
	{
		return ExtendedBuild(BuildOptions, FTransformSequence3d(), false);
	}

	virtual bool ExtendedBuild(const FMeshSceneAdapterBuildOptions& BuildOptions, const FTransformSequence3d& TransformSeq, bool bBakeTransform)
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
			// does not need this - copypasta
			TUniquePtr<FMeshDescriptionTriangleMeshAdapter> Adapter = MakeUnique<FMeshDescriptionTriangleMeshAdapter>(SourceMesh);

			FVector3d BuildScale;
#if WITH_EDITOR
			// respect BuildScale build setting
			const FMeshBuildSettings& LODBuildSettings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;
			BuildScale = (FVector3d)LODBuildSettings.BuildScale3D;
			Adapter->SetBuildScale(BuildScale, false);
#endif
			FDynamicMesh3 ConvertedMesh = FastConvertToDynamicMesh<FMeshDescriptionTriangleMeshAdapter>(*Adapter);

			if (bBakeTransform)
			{
				MeshTransforms::ApplyTransform(ConvertedMesh,
					[&](const FVector3d& P) { return TransformSeq.TransformPosition(P); },
					[&](const FVector3f& N) { return (FVector3f)TransformSeq.TransformNormal((FVector3d)N); });
			}

			if (BuildOptions.bThickenThinMeshes)
			{
				FFrame3d ThinMeshPlane;
				bool IsThinMesh = IsThinPlanarMesh<FMeshDescriptionTriangleMeshAdapter>(*Adapter, BuildOptions.DesiredMinThickness, ThinMeshPlane);
				if (IsThinMesh)
				{
					ThickenThinPlanarMesh(ConvertedMesh, BuildOptions.DesiredMinThickness, ThinMeshPlane);
				}
			}

			DynamicWrapper = MakeUnique<FDynamicMeshSpatialWrapper>();
			DynamicWrapper->Mesh = MoveTemp(ConvertedMesh);
			DynamicWrapper->bHasBakedTransform = bBakeTransform;
			DynamicWrapper->Build(BuildOptions);

			return true;
		}

		SourceMesh = nullptr;
		return false;
	}

	virtual int32 GetTriangleCount() override
	{
		return (SourceMesh) ? DynamicWrapper->GetTriangleCount() : 0;
	}

	virtual FAxisAlignedBox3d GetWorldBounds(TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		return (SourceMesh) ? DynamicWrapper->GetWorldBounds(LocalToWorldFunc) : FAxisAlignedBox3d::Empty();
	}

	virtual void CollectSeedPoints(TArray<FVector3d>& WorldPoints, TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) override
	{
		if (SourceMesh)
		{
			DynamicWrapper->CollectSeedPoints(WorldPoints, LocalToWorldFunc);
		}
	}

	virtual double FastWindingNumber(const FVector3d& P, const FTransformSequence3d& LocalToWorldTransform) override
	{
		return (SourceMesh) ? DynamicWrapper->FastWindingNumber(P, LocalToWorldTransform) : 0.0;
	}

	virtual void ProcessVerticesInWorld(TFunctionRef<void(const FVector3d&)> ProcessFunc, const FTransformSequence3d& LocalToWorldTransform) override
	{
		if (SourceMesh)
		{
			DynamicWrapper->ProcessVerticesInWorld(ProcessFunc, LocalToWorldTransform);
		}
	}


	virtual void AppendMesh(FDynamicMesh3& AppendTo, const FTransformSequence3d& TransformSeq) override
	{
		if (SourceMesh)
		{
			DynamicWrapper->AppendMesh(AppendTo, TransformSeq);
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
			AABBTree = MakeUnique<TMeshAABBTree3<FMeshDescriptionTriangleMeshAdapter>>(Adapter.Get(), true);
			FWNTree = MakeUnique<TFastWindingTree<FMeshDescriptionTriangleMeshAdapter>>(AABBTree.Get(), true);
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

		if (WrapperInfo->SourceContainer.MeshType == ESceneMeshType::StaticMeshAsset)
		{
			UStaticMesh* StaticMesh = WrapperInfo->SourceContainer.GetStaticMesh();
			int32 LODIndex = 0;
			bool bDecompose = CheckIfStaticMeshUsageRequiresDynamicMeshDecomposition(
				StaticMesh, LODIndex,
				BuildOptions,
				WrapperInfo->ParentMeshes, WrapperInfo->NonUniformScaleCount);

			if (bDecompose)
			{
				DecomposedSourceMeshCount++;

				TArray<FActorChildMesh*> MeshesToDecompose = WrapperInfo->ParentMeshes;
				for (FActorChildMesh* MeshInstance : MeshesToDecompose)
				{
					TSharedPtr<FSpatialWrapperInfo> NewWrapperInfo = MakeShared<FSpatialWrapperInfo>();
					NewWrapperInfo->SourceContainer = MeshInstance->MeshContainer;

					TUniquePtr<FStaticMeshDynamicSpatialWrapper> DynamicMeshWrapper = MakeUnique<FStaticMeshDynamicSpatialWrapper>();
					DynamicMeshWrapper->SourceContainer = NewWrapperInfo->SourceContainer;
					DynamicMeshWrapper->StaticMesh = StaticMesh;
					DynamicMeshWrapper->ExtendedBuild(BuildOptions, MeshInstance->WorldTransform, true);
					NewWrapperInfo->SpatialWrapper = MoveTemp(DynamicMeshWrapper);
					MeshInstance->MeshSpatial = NewWrapperInfo->SpatialWrapper.Get();

					ListsLock.Lock();
					void* OldMeshKey = MeshInstance->MeshContainer.GetMeshKey();
					SpatialAdapters.Remove(OldMeshKey);			// remove existing
					void* NewMeshKey = (void*)MeshInstance;		// need to key on instance, not unique mesh
					SpatialAdapters.Add(NewMeshKey, NewWrapperInfo);
					AddedTrisCount += NewWrapperInfo->SpatialWrapper->GetTriangleCount();
					ListsLock.Unlock();

					DecomposedMeshesCount++;
				}
				return;
			}
		}

		TUniquePtr<IMeshSpatialWrapper>& Wrapper = WrapperInfo->SpatialWrapper;
		bool bOK = Wrapper->Build(BuildOptions);
		ensure(bOK);	// assumption is that the wrapper will handle failure gracefully
	});

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

double FMeshSceneAdapter::FastWindingNumber(const FVector3d& P)
{
	double SumWinding = 0.0;
	for (const TUniquePtr<FActorAdapter>& Actor : SceneActors)
	{
		// TODO: cannot skip here because fast winding number for open region will extend...
		if ( true /**Actor->WorldBounds.Contains(P)*/ )
		{
			for (const TUniquePtr<FActorChildMesh>& ChildMesh : Actor->ChildMeshes)
			{
				if (ChildMesh->MeshSpatial != nullptr)
				{
					double MeshWinding = ChildMesh->MeshSpatial->FastWindingNumber(P, ChildMesh->WorldTransform);
					SumWinding += MeshWinding;
				}
			}
		}
	}
	return SumWinding;
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
	FinalHullSolver.Solve(WorldHullPoints);
	FPolygon2d ConvexHullPoly;
	for (int32 idx : FinalHullSolver.GetPolygonIndices())
	{
		ConvexHullPoly.AppendVertex(WorldHullPoints[idx]);
	}

	FPlanarPolygonMeshGenerator MeshGen;
	MeshGen.Polygon = ConvexHullPoly;
	FDynamicMesh3 BasePolygonMesh(&MeshGen.Generate());
	MeshTransforms::Translate(BasePolygonMesh, ZRange.Min * FVector3d::UnitZ());

	if (ExtrudeHeight == 0)
	{
		BasePolygonMesh.ReverseOrientation();		// flip so it points down
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

	TUniquePtr<FActorAdapter> Adapter = MakeUnique<FActorAdapter>();
	Adapter->SourceActor = nullptr;

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
	Adapter->ChildMeshes.Add(MoveTemp(ChildMesh));
	SceneActors.Add(MoveTemp(Adapter));

}

