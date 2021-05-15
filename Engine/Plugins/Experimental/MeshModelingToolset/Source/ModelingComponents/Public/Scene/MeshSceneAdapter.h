// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "BoxTypes.h"
#include "TransformTypes.h"
#include "TransformSequence.h"
#include "DynamicMesh3.h"

class AActor;
class UActorComponent;
class UStaticMesh;

namespace UE
{
namespace Geometry
{


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
 * Configuration for FMeshSceneAdapter::Build()
 */
struct FMeshSceneAdapterBuildOptions
{
	bool bPrintDebugMessages = false;

	/** If true, find approximately-planar meshes with a main dimension below DesiredMinThickness and thicken them to DesiredMinThickness  */
	bool bThickenThinMeshes = false;
	/** Thickness used for bThickenThinMeshes processing */
	double DesiredMinThickness = 0.1;
};





/**
 * Abstract interface to a spatial data structure for a mesh
 */
class MODELINGCOMPONENTS_API IMeshSpatialWrapper
{
public:
	virtual ~IMeshSpatialWrapper() {}

	FMeshTypeContainer SourceContainer;

	/** If possible, spatial data structure should defer construction until this function, which will be called off the game thread (in ParallelFor) */
	virtual bool Build(const FMeshSceneAdapterBuildOptions& BuildOptions) = 0;

	/*** @return triangle count for this mesh */
	virtual int32 GetTriangleCount() = 0;

	/** Calculate bounding box for this Mesh */
	virtual FAxisAlignedBox3d GetWorldBounds(TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) = 0;

	/** Calculate the mesh winding number at the given Position. Must be callable in parallel from any thread.  */
	virtual double FastWindingNumber(const FVector3d& P, const FTransformSequence3d& LocalToWorldTransform) = 0;

	/** Collect a set of seed points from this Mesh, mapped through LocalToWorldFunc to world space. Must be callable in parallel from any thread. */
	virtual void CollectSeedPoints(TArray<FVector3d>& WorldPoints, TFunctionRef<FVector3d(const FVector3d&)> LocalToWorldFunc) = 0;

	/** apply ProcessFunc to each vertex in world space */
	virtual void ProcessVerticesInWorld(TFunctionRef<void(const FVector3d&)> ProcessFunc, const FTransformSequence3d& LocalToWorldTransform) = 0;

	/** Append the geometry represented by this wrapper to the accumulated AppendTo mesh, under the given world transform */
	virtual void AppendMesh(FDynamicMesh3& AppendTo, const FTransformSequence3d& TransformSeq) = 0;
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

	InternallyGeneratedComponent,

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
	FActorChildMesh() {}
	FActorChildMesh(const FActorChildMesh&) = delete;		// must delete to allow TArray<TUniquePtr> in FActorAdapter
	FActorChildMesh(FActorChildMesh&&) = delete;

	/** the Component this Mesh was generated from, if there is one. */
	UActorComponent* SourceComponent = nullptr;
	/** Type of SourceComponent, if known */
	EActorMeshComponentType ComponentType = EActorMeshComponentType::Unknown;
	/** Index of this Mesh in the SourceComponent, if such an index exists (eg Instance Index in InstancedStaticMeshComponent) */
	int32 ComponentIndex = 0;

	/** Wrapper around the Mesh this FActorChildMesh refers to (eg from a StaticMeshAsset, etc) */
	FMeshTypeContainer MeshContainer;
	/** Local-to-World transformation of the Mesh in the MeshContainer */
	FTransformSequence3d WorldTransform;
	bool bIsNonUniformScaled = false;

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
	TArray<TUniquePtr<FActorChildMesh>> ChildMeshes;
	// World-space bounds of this Actor (meshes)
	FAxisAlignedBox3d WorldBounds;
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

	/** Add the given Actors to our Actor set. */
	void AddActors(const TArray<AActor*>& ActorsSetIn);

	/** Build */
	void Build(const FMeshSceneAdapterBuildOptions& BuildOptions);

	/**
	 * Generate a new mesh that "caps" the mesh scene on the bottom. This can be used in cases where
	 * the geometry is open on the base, to fill in the hole, which allows things like mesh-solidification
	 * to work better. The base mesh is a polygon which can be optionally extruded.
	 * Currently the closing mesh is a convex hull, todo: implement a better option
	 * @param BaseHeight the height in world units from the bounding-box MinZ to consider as part of the "base".
	 * @param ExtrudeHeight height in world units to extrude the generated base. Positive is in +Z direction. If zero, an open-boundary polygon is generated instead.
	 */
	void GenerateBaseClosingMesh(double BaseHeight = 1.0, double ExtrudeHeight = 0.0);

	/**
	 * Statistics about the mesh scene returned by GetGeometryStatistics()
	 */
	struct FStatistics
	{
		int32 UniqueMeshCount = 0;
		int32 UniqueMeshTriangleCount = 0;

		int32 InstanceMeshCount = 0;
		int32 InstanceMeshTriangleCount = 0;
	};

	/** @return bounding box for the Actor set */
	virtual void GetGeometryStatistics(FStatistics& StatsOut);

	/** @return bounding box for the Actor set */
	virtual FAxisAlignedBox3d GetBoundingBox();

	/** @return a set of points on the surface of the meshes, can be used to initialize the MarchingCubes mesher */
	virtual void CollectMeshSeedPoints(TArray<FVector3d>& PointsOut);

	/** @return FastWindingNumber computed across all mesh Actors/Components */
	virtual double FastWindingNumber(const FVector3d& P);

	/** Append all instance triangles to a single mesh. May be very large. */
	virtual void GetAccumulatedMesh(FDynamicMesh3& AccumMesh);

protected:

	// top-level list of ActorAdapters, which represent each Actor and set of Components
	TArray<TUniquePtr<FActorAdapter>> SceneActors;

	struct FSpatialWrapperInfo
	{
		FMeshTypeContainer SourceContainer;
		TArray<FActorChildMesh*> ParentMeshes;
		int32 NonUniformScaleCount = 0;
		TUniquePtr<IMeshSpatialWrapper> SpatialWrapper;
	};

	// Unique set of spatial data structure query interfaces, one for each Mesh object, which is identified by void* pointer
	TMap<void*, TSharedPtr<FSpatialWrapperInfo>> SpatialAdapters;

	bool bEnableClipPlane = false;
	FFrame3d ClipPlane;
};


}
}
