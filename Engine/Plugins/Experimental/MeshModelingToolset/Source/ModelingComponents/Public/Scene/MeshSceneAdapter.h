// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "BoxTypes.h"
#include "TransformTypes.h"
#include "TransformSequence.h"

class AActor;
class UActorComponent;
class UStaticMesh;

namespace UE
{
namespace Geometry
{


/**
 * Abstract interface to a spatial data structure for a mesh
 */
class MODELINGCOMPONENTS_API IMeshSpatialWrapper
{
public:
	virtual ~IMeshSpatialWrapper() {}

	/** If possible, spatial data structure should defer construction until this function, which will be called off the game thread (in ParallelFor) */
	virtual bool Build() = 0;

	/** Calculate the mesh winding number at the given Position. Must be callable in parallel from any thread.  */
	virtual double FastWindingNumber(const FVector3d& P) = 0;

	/** Collect a set of seed points from this Mesh, mapped through LocalToWorldFunc to world space. Must be callable in parallel from any thread. */
	virtual void CollectSeedPoints(TArray<FVector3d>& WorldPoints, TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) = 0;
};


/**
 * ESceneMeshType is used to indicate which type of Mesh a FMeshTypeContainer contains.
 */
enum class ESceneMeshType
{
	StaticMeshAsset,
	Unknown
};


/**
 * FMeshTypeContainer is a wrapper for an object that contains a unique Mesh of some kind,
 * which is used by an FActorChildMesh to represent that unique mesh. For example this could be 
 * a UStaticMesh asset, a FMeshDescription, a FDynamicMesh3, and so on.
 * (Currently only UStaticMesh is supported)
 */
struct MODELINGCOMPONENTS_API FMeshTypeContainer
{
	/** raw pointer to the Mesh, used as a key to identify this mesh in various maps/etc */
	void* MeshPointer = nullptr;
	/** type of unique Mesh object this container contains */
	ESceneMeshType MeshType = ESceneMeshType::Unknown;

	/** @return key for this mesh */
	void* GetMeshKey() const { return MeshPointer; }

	/** @return the UStaticMesh this container contains, if this is a StaticMeshAsset container (otherwise nullptr) */
	UStaticMesh* GetStaticMesh() const
	{
		if (ensure(MeshType == ESceneMeshType::StaticMeshAsset))
		{
			return (UStaticMesh*)MeshPointer;
		}
		return nullptr;
	}
};


/**
 * EActorMeshComponentType enum is used to determine which type of Component 
 * an FActorChildMesh represents.
 */
enum class EActorMeshComponentType
{
	StaticMesh,
	InstancedStaticMesh,
	HierarchicalInstancedStaticMesh,

	Unknown
};


/**
 * FActorChildMesh represents a 3D Mesh attached to an Actor. This generally comes from a Component,
 * however in some cases a Component generates multiple FActorChildMesh (eg an InstancedStaticMeshComponent),
 * and potentially some Actors may store/represent a Mesh directly (no examples currently).
 */
struct MODELINGCOMPONENTS_API FActorChildMesh
{
public:
	/** the Component this Mesh was generated from, if there is one. */
	UActorComponent* SourceComponent = nullptr;
	/** Type of SourceComponent, if known */
	EActorMeshComponentType ComponentType = EActorMeshComponentType::Unknown;
	/** Index of this Mesh in the SourceComponent, if such an index exists (eg Instance Index in InstancedStaticMeshComponent) */
	int32 ComponentIndex = 0;

	/** Wrapper around the Mesh this FActorChildMesh refers to (eg from a StaticMeshAsset, etc) */
	FMeshTypeContainer MeshContainer;
	/** Local-to-World transformation of the Mesh in the MeshContainer */
	UE::Geometry::FTransformSequence3d WorldTransform;
	UE::Geometry::FTransformSequence3d WorldTransformInverse;

	/** Spatial data structure that represents the Mesh in MeshContainer - assumption is this is owned externally */
	IMeshSpatialWrapper* MeshSpatial;
};

/**
 * FActorAdapter is used by FMeshSceneAdapter to represent all the child info for an AActor.
 * This is primarily a list of FActorChildMesh, which represent the spatially-positioned meshes
 * of any child StaticMeshComponents or other mesh Components that can be identified and represented.
 * Note that ChildActorComponents will be flatted into the parent Actor.
 */
struct MODELINGCOMPONENTS_API FActorAdapter
{
public:
	FActorAdapter() {}
	FActorAdapter(const FActorAdapter&) = delete;		// must delete to allow TArray<TUniquePtr> in FMeshSceneAdapter
	FActorAdapter(FActorAdapter&&) = delete;

	// the AActor this Adapter represents
	AActor* SourceActor = nullptr;
	// set of child Meshes with transforms
	TArray<FActorChildMesh> ChildMeshes;
	// World-space bounds of this Actor (meshes)
	UE::Geometry::FAxisAlignedBox3d WorldBounds;
};


/**
 * FMeshSceneAdapter creates an internal representation of an Actor/Component/Asset hierarchy,
 * so that a minimal set of Mesh data structures can be constructed for the unique Meshes (generally Assets).
 * This allows queries against the Actor set to be computed without requiring mesh copies or 
 * duplicates of the mesh data structures (ie, saving memory, at the cost of some computation overhead).
 * 
 * Currenly this builds an AABBTree and FastWindingTree for each unique Mesh,
 * and only FastWinding and "Seed Point" queries are (currently) exposed.
 * (SeedPoint query is necessary for mesh the scalar Winding field)
 */
class MODELINGCOMPONENTS_API FMeshSceneAdapter
{
public:
	virtual ~FMeshSceneAdapter() {}

	FMeshSceneAdapter() {}
	FMeshSceneAdapter(const FMeshSceneAdapter&) = delete;		// must delete this due to TArray<TUniquePtr> member
	FMeshSceneAdapter(FMeshSceneAdapter&&) = delete;

	/** Add the  given Actors to our Actor set */
	void AddActors(const TArray<AActor*>& ActorsSetIn);

	/** @return bounding box for the Actor set */
	virtual UE::Geometry::FAxisAlignedBox3d GetBoundingBox();

	/** @return a set of points on the surface of the meshes, can be used to initialize the MarchingCubes mesher */
	virtual void CollectMeshSeedPoints(TArray<FVector3d>& PointsOut);

	/** @return max FastWindingNumber computed for all mesh Actors/Components */
	virtual double MaxFastWindingNumber(const FVector3d& P);

protected:

	// top-level list of ActorAdapters, which represent each Actor and set of Components
	TArray<TUniquePtr<FActorAdapter>> SceneActors;

	// Unique set of spatial data structure query interfaces, one for each Mesh object, which is identified by void* pointer
	TMap<void*, TSharedPtr<IMeshSpatialWrapper>> SpatialAdapters;
};


}
}
