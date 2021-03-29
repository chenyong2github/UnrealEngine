// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidClusteredParticles.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/Transform.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/ExternalCollisionData.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/ClusterCreationParameters.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Framework/BufferedData.h"

#define TODO_CONVERT_GEOMETRY_COLLECTION_PARTICLE_INDICES_TO_PARTICLE_POINTERS 0

namespace Chaos
{
	extern CHAOS_API FRealSingle ChaosClusteringChildrenInheritVelocity;
}

namespace Chaos
{

class CHAOS_API FClusterBuffer
{
public:
	using FClusterChildrenMap = TMap<FPBDRigidParticleHandle*, TArray<FPBDRigidParticleHandle*>>;
	using FClusterTransformMap = TMap<FPBDRigidParticleHandle*, FRigidTransform3>;

	virtual ~FClusterBuffer() = default;

	FClusterChildrenMap MChildren;
	FClusterTransformMap ClusterParentTransforms;
	TArray<Chaos::TSerializablePtr<FImplicitObject>> GeometryPtrs;
};

/* 
* PDBRigidClustering
*/
template<class T_FPBDRigidEvolution, class T_FPBDCollisionConstraint>
class CHAOS_API TPBDRigidClustering
{
	typedef typename T_FPBDCollisionConstraint::FPointContactConstraint FPointContactConstraint;
public:
	/** Parent to children */
	typedef TMap<FPBDRigidParticleHandle*, TArray<FPBDRigidParticleHandle*> > FClusterMap;

	using FCollisionConstraintHandle = FPBDCollisionConstraintHandle;

	TPBDRigidClustering(T_FPBDRigidEvolution& InEvolution, FPBDRigidClusteredParticles& InParticles);
	~TPBDRigidClustering();

	//
	// Initialization
	//

	/**
	 *  Initialize a cluster with the specified children.
	 *
	 *	\p ClusterGroupIndex - Index to join cluster into.
	 *	\p Children - List of children that should belong to the cluster
	 *	\p Parameters - ClusterParticleHandle must be valid, this is the parent cluster body.
	 *	ProxyGeometry : Collision default for the cluster, automatically generated otherwise.
	 *		ForceMassOrientation : Inertial alignment into mass space.
	 *	
	 */
	Chaos::FPBDRigidClusteredParticleHandle* CreateClusterParticle(
		const int32 ClusterGroupIndex, 
		TArray<Chaos::FPBDRigidParticleHandle*>&& Children, 
		const FClusterCreationParameters& Parameters = FClusterCreationParameters(),
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry = nullptr,
		const FRigidTransform3* ForceMassOrientation = nullptr,
		const FUniqueIdx* ExistingIndex = nullptr);

	/**
	 *  CreateClusterParticleFromClusterChildren
	 *    Children : Rigid body ID to include in the cluster.
	 */
	Chaos::FPBDRigidClusteredParticleHandle* CreateClusterParticleFromClusterChildren(
		TArray<FPBDRigidParticleHandle*>&& Children, 
		FPBDRigidClusteredParticleHandle* Parent,
		const FRigidTransform3& ClusterWorldTM, 
		const FClusterCreationParameters& Parameters/* = FClusterCreationParameters()*/);

	/**
	 *  UnionClusterGroups
	 *    Clusters that share a group index should be unioned into a single cluster prior to simulation.
	 *    The GroupIndex should be set on creation, and never touched by the client again.
	 */
	void UnionClusterGroups();

	//
	// Releasing
	//

	/*
	*  DeactivateClusterParticle
	*    Release all the particles within the cluster particle
	*/
	TSet<FPBDRigidParticleHandle*> DeactivateClusterParticle(FPBDRigidClusteredParticleHandle* ClusteredParticle);

	/*
	*  ReleaseClusterParticles (BasedOnStrain)
	*    Release clusters based on the passed in \p ExternalStrainArray, or the 
	*    particle handle's current \c CollisionImpulses() value. Any cluster bodies 
	*    that have a strain value less than this valid will be released from the 
	*    cluster.
	*/
	TSet<FPBDRigidParticleHandle*> ReleaseClusterParticles(
		FPBDRigidClusteredParticleHandle* ClusteredParticle, 
		const TMap<FGeometryParticleHandle*, FReal>* ExternalStrainMap = nullptr,
		bool bForceRelease = false);

	TSet<FPBDRigidParticleHandle*> ReleaseClusterParticlesNoInternalCluster(
		FPBDRigidClusteredParticleHandle* ClusteredParticle,
		const TMap<FGeometryParticleHandle*, FReal>* ExternalStrainMap = nullptr,
		bool bForceRelease = false);

	/*
	*  ReleaseClusterParticles
	*    Release all rigid body IDs passed,
	*/
	TSet<FPBDRigidParticleHandle*> ReleaseClusterParticles(
		TArray<FPBDRigidParticleHandle*> ChildrenParticles);

	//
	// Operational 
	//

	/*
	*  AdvanceClustering
	*   Advance the cluster forward in time;
	*   ... Union unprocessed geometry.
	*   ... Release bodies based collision impulses.
	*   ... Updating properties as necessary.
	*/
	void AdvanceClustering(const FReal dt, T_FPBDCollisionConstraint& CollisionRule);

	/**
	*  BreakingModel
	*    Implements the promotion breaking model, where strain impulses are
	*    summed onto the cluster body, and released if greater than the
	*    encoded strain. The remainder strains are propagated back down to
	*    the children clusters.
	*/
	TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> BreakingModel(
		TMap<FGeometryParticleHandle*, FReal>* ExternalStrainMap = nullptr);

	/**
	*  PromoteStrains
	*    Sums the strains based on the cluster hierarchy. For example
	*    a cluster with two children that have strains {3,4} will have
	*    a ExternalStrain entry of 7. Will only decent the current
	*    node passed, and ignores the disabled flag.
	*/
	FReal PromoteStrains(FPBDRigidParticleHandle* CurrentNode);

	/*
	*  Process the kinematic state of the clusters. Because the leaf node geometry can
	*  be changed by the solver, it is necessary to check all the sub clusters.
	*/
	void UpdateKinematicProperties(FPBDRigidParticleHandle* Parent);

	//
	// Access
	//
	//  The ClusterIds and ChildrenMap are shared resources that can
	//  be accessed via the game thread.
	//
	const FClusterBuffer&  GetBufferedData() const { ResourceLock.ReadLock(); return BufferResource; } /* Secure access from game thread*/
	void                   ReleaseBufferedData() const { ResourceLock.ReadUnlock(); }    /* Release access from game thread*/
	void                   SwapBufferedData();                                         /* Managed by the PBDRigidSolver ONLY!*/


	/*
	*  GetActiveClusterIndex
	*    Get the current childs active cluster. Returns INDEX_NONE if
	*    not active or driven.
	*/
	FPBDRigidParticleHandle* GetActiveClusterIndex(FPBDRigidParticleHandle* Child);

	/*
	*  GetClusterIdsArray
	*    The cluster ids provide a mapping from the rigid body index
	*    to its parent cluster id. The parent id might not be the
	*    active id, see the GetActiveClusterIndex to find the active cluster.
	*    INDEX_NONE represents a non-clustered body.
	*/
	TArrayCollectionArray<ClusterId>&       GetClusterIdsArray() { return MParticles.ClusterIdsArray(); }
	const TArrayCollectionArray<ClusterId>& GetClusterIdsArray() const { return MParticles.ClusterIdsArray(); }

	/*
	*  GetInternalClusterArray
	*    The internal cluster array indicates if this cluster was generated internally
	*    and would no be owned by an external source.
	*/
	const TArrayCollectionArray<bool>& GetInternalClusterArray() const { return MParticles.InternalClusterArray(); }

	/*
	*  GetChildToParentMap
	*    This map stores the relative transform from a child to its cluster parent.
	*/
	const TArrayCollectionArray<FRigidTransform3>& GetChildToParentMap() const { return MParticles.ChildToParentArray(); }

	/*
	*  GetStrainArray
	*    The strain array is used to store the maximum strain allowed for a individual
	*    body in the simulation. This attribute is initialized during the creation of
	*    the cluster body, can be updated during the evaluation of the simulation.
	*/
	TArrayCollectionArray<FReal>& GetStrainArray() { return MParticles.StrainsArray(); }

	/**
	*  GetParentToChildren
	*    The parent to children map stores the currently active cluster ids (Particle Indices) as
	*    the keys of the map. The value of the map is a pointer to an array  constrained
	*    rigid bodies.
	*/
	FClusterMap &       GetChildrenMap() { return MChildren; }
	const FClusterMap & GetChildrenMap() const { return MChildren; }

	/*
	*  GetClusterGroupIndexArray
	*    The group index is used to automatically bind disjoint clusters. This attribute it set
	*    during the creation of cluster to a positive integer value. During UnionClusterGroups (which
	*    is called during AdvanceClustering) the positive bodies are joined with a negative pre-existing
	*    body, then set negative. Zero entries are ignored within the union.
	*/
	TArrayCollectionArray<int32>& GetClusterGroupIndexArray() { return MParticles.ClusterGroupIndexArray(); }

	/** Indicates if the child geometry is approximated by a single proxy */
	const TArrayCollectionArray<FMultiChildProxyId>& GetMultiChildProxyIdArray() const { return MParticles.MultiChildProxyIdArray(); }

	/** If multi child proxy is used, this is the data needed */
	const TArrayCollectionArray<TUniquePtr<TMultiChildProxyData<FReal, 3>>>& GetMultiChildProxyDataArray() const { return MParticles.MultiChildProxyDataArray(); }

	void AddToClusterUnion(int32 ClusterID, FPBDRigidClusteredParticleHandle* Handle)
	{
		if(ClusterID <= 0)
		{
			return;
		}

		if(!ClusterUnionMap.Contains(ClusterID))
		{
			ClusterUnionMap.Add(ClusterID, TArray<FPBDRigidClusteredParticleHandle*>());
		}

		ClusterUnionMap[ClusterID].Add(Handle);
	}

	const TArray<FBreakingData>& GetAllClusterBreakings() const { return MAllClusterBreakings; }
	void SetGenerateClusterBreaking(bool DoGenerate) { DoGenerateBreakingData = DoGenerate; }
	void ResetAllClusterBreakings() { MAllClusterBreakings.Reset(); }

	/*
	* GetConnectivityEdges
	*    Provides a list of each rigid body's current siblings and associated strain within the cluster.
	*/
	const TArrayCollectionArray<TArray<TConnectivityEdge<FReal>>>& GetConnectivityEdges() const { return MParticles.ConnectivityEdgesArray(); }

	/**
	* GenerateConnectionGraph
	*   Creates a connection graph for the given index using the creation parameters. This will not
	*   clear the existing graph.
	*/
	void SetClusterConnectionFactor(FReal ClusterConnectionFactorIn) { MClusterConnectionFactor = ClusterConnectionFactorIn; }
	void SetClusterUnionConnectionType(FClusterCreationParameters::EConnectionMethod ClusterConnectionType) { MClusterUnionConnectionType = ClusterConnectionType; }

	void GenerateConnectionGraph(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters & Parameters = FClusterCreationParameters());

	const TSet<Chaos::FPBDRigidClusteredParticleHandle*>& GetTopLevelClusterParents() const { return TopLevelClusterParents; }
	TSet<Chaos::FPBDRigidClusteredParticleHandle*>& GetTopLevelClusterParents() { return TopLevelClusterParents; }

	/* Ryan - do we still need this?
	void InitTopLevelClusterParents(const int32 StartIndex)
	{
		if (!StartIndex)
		{
			TopLevelClusterParents.Reset();
		}
		for (uint32 i = StartIndex; i < MParticles.Size(); ++i)
		{
			if (MParticles.ClusterIds(i).Id == INDEX_NONE && !MParticles.Disabled(i))
			{
				TopLevelClusterParents.Add(i);
			}
		}
	}
	*/
 protected:
	void UpdateMassProperties(
		Chaos::FPBDRigidClusteredParticleHandle* Parent, 
		TSet<FPBDRigidParticleHandle*>& Children, 
		const FRigidTransform3* ForceMassOrientation);
	void UpdateGeometry(
		Chaos::FPBDRigidClusteredParticleHandle* Parent, 
		const TSet<FPBDRigidParticleHandle*>& Children, 
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry,
		const FClusterCreationParameters& Parameters);

	void ComputeStrainFromCollision(const T_FPBDCollisionConstraint& CollisionRule);
	void ResetCollisionImpulseArray();
	void DisableCluster(FPBDRigidClusteredParticleHandle* ClusteredParticle);
	void DisableParticleWithBreakEvent(Chaos::FPBDRigidParticleHandle* Particle);

	/*
	* Connectivity
	*/
	void UpdateConnectivityGraphUsingPointImplicit(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters& Parameters = FClusterCreationParameters());
	void FixConnectivityGraphUsingDelaunayTriangulation(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters& Parameters = FClusterCreationParameters());
	void UpdateConnectivityGraphUsingDelaunayTriangulation(
		Chaos::FPBDRigidClusteredParticleHandle* Parent,
		const FClusterCreationParameters& Parameters = FClusterCreationParameters());

	void ConnectNodes(
		FPBDRigidParticleHandle* Child1,
		FPBDRigidParticleHandle* Child2);
	void ConnectNodes(
		FPBDRigidClusteredParticleHandle* Child1,
		FPBDRigidClusteredParticleHandle* Child2);

	void RemoveNodeConnections(FPBDRigidParticleHandle* Child);
	void RemoveNodeConnections(FPBDRigidClusteredParticleHandle* Child);

private:

	T_FPBDRigidEvolution& MEvolution;
	FPBDRigidClusteredParticles& MParticles;
	TSet<Chaos::FPBDRigidClusteredParticleHandle*> TopLevelClusterParents;
	TSet<Chaos::FPBDRigidParticleHandle*> MActiveRemovalIndices;


	// Cluster data
	mutable FRWLock ResourceLock;
	FClusterBuffer BufferResource;
	FClusterMap MChildren;
	TMap<int32, TArray<FPBDRigidClusteredParticleHandle*> > ClusterUnionMap;


	// Collision Impulses
	bool MCollisionImpulseArrayDirty;
	
	// Breaking data
	bool DoGenerateBreakingData;
	TArray<FBreakingData> MAllClusterBreakings;

	FReal MClusterConnectionFactor;
	FClusterCreationParameters::EConnectionMethod MClusterUnionConnectionType;
};

void UpdateClusterMassProperties(
	Chaos::FPBDRigidClusteredParticleHandle* Parent,
	TSet<FPBDRigidParticleHandle*>& Children,
	const FRigidTransform3* ForceMassOrientation = nullptr);

inline TArray<FVec3> CleanCollisionParticles(
	const TArray<FVec3>& Vertices,
	FAABB3 BBox, 
	const FReal SnapDistance=(FReal)0.01)
{
	const int32 NumPoints = Vertices.Num();
	if (NumPoints <= 1)
		return TArray<FVec3>(Vertices);

	FReal MaxBBoxDim = BBox.Extents().Max();
	if (MaxBBoxDim < SnapDistance)
		return TArray<FVec3>(&Vertices[0], 1);

	BBox.Thicken(FMath::Max(SnapDistance/10, KINDA_SMALL_NUMBER*10)); // 0.001
	MaxBBoxDim = BBox.Extents().Max();

	const FVec3 PointsCenter = BBox.Center();
	TArray<FVec3> Points(Vertices);

	// Find coincident vertices.  We hash to a grid of fine enough resolution such
	// that if 2 particles hash to the same cell, then we're going to consider them
	// coincident.
	TSet<int64> OccupiedCells;
	OccupiedCells.Reserve(NumPoints);

	TArray<int32> Redundant;
	Redundant.Reserve(NumPoints); // Excessive, but ensures consistent performance.

	int32 NumCoincident = 0;
	const int64 Resolution = static_cast<int64>(floor(MaxBBoxDim / FMath::Max(SnapDistance,KINDA_SMALL_NUMBER)));
	const FReal CellSize = MaxBBoxDim / Resolution;
	for (int32 i = 0; i < 2; i++)
	{
		Redundant.Reset();
		OccupiedCells.Reset();
		// Shift the grid by 1/2 a grid cell the second iteration so that
		// we don't miss slightly adjacent coincident points across cell
		// boundaries.
		const FVec3 GridCenter = FVec3(0) - FVec3(i * CellSize / 2);
		for (int32 j = 0; j < Points.Num(); j++)
		{
			const FVec3 Pos = Points[j] - PointsCenter; // Centered at the origin
			const TVec3<int64> Coord(
				static_cast<int64>(floor((Pos[0] - GridCenter[0]) / CellSize + Resolution / 2)),
				static_cast<int64>(floor((Pos[1] - GridCenter[1]) / CellSize + Resolution / 2)),
				static_cast<int64>(floor((Pos[2] - GridCenter[2]) / CellSize + Resolution / 2)));
			const int64 FlatIdx =
				((Coord[0] * Resolution + Coord[1]) * Resolution) + Coord[2];

			bool AlreadyInSet = false;
			OccupiedCells.Add(FlatIdx, &AlreadyInSet);
			if (AlreadyInSet)
				Redundant.Add(j);
		}

		for (int32 j = Redundant.Num(); j--;)
		{
			Points.RemoveAt(Redundant[j]);
		}
	}

	// Shrink the array, if appropriate
	Points.SetNum(Points.Num(), true);
	return Points;
}

inline TArray<FVec3> CleanCollisionParticles(
	const TArray<FVec3>& Vertices, 
	const FReal SnapDistance=(FReal)0.01)
{
	if (!Vertices.Num())
	{
		return TArray<FVec3>();
	}
	FAABB3 BBox(FAABB3::EmptyAABB());
	for (const FVec3& Pt : Vertices)
	{
		BBox.GrowToInclude(Pt);
	}
	return CleanCollisionParticles(Vertices, BBox, SnapDistance);
}

inline TArray<FVec3> CleanCollisionParticles(
	FTriangleMesh &TriMesh, 
	const TArrayView<const FVec3>& Vertices, 
	const FReal Fraction)
{
	TArray<FVec3> CollisionVertices;
	if (Fraction <= 0.0)
		return CollisionVertices;

	// If the tri mesh has any open boundaries, see if we can merge any coincident
	// vertices on the boundary.  This makes the importance ordering work much better
	// as we need the curvature at each edge of the tri mesh, and we can't calculate
	// curvature on discontiguous triangles.
	TSet<int32> BoundaryPoints = TriMesh.GetBoundaryPoints();
	if (BoundaryPoints.Num())
	{
		TMap<int32, int32> Remapping =
			TriMesh.FindCoincidentVertexRemappings(BoundaryPoints.Array(), Vertices);
		TriMesh.RemapVertices(Remapping);
	}

	// Get the importance vertex ordering, from most to least.  Reorder the 
	// particles accordingly.
	TArray<int32> CoincidentVertices;
	const TArray<int32> Ordering = TriMesh.GetVertexImportanceOrdering(Vertices, &CoincidentVertices, true);

	// Particles are ordered from most important to least, with coincident 
	// vertices at the very end.
	const int32 NumGoodPoints = Ordering.Num() - CoincidentVertices.Num();

#if DO_GUARD_SLOW
	for (int i = NumGoodPoints; i < Ordering.Num(); ++i)
	{
		ensure(CoincidentVertices.Contains(Ordering[i]));	//make sure all coincident vertices are at the back
	}
#endif

	CollisionVertices.AddUninitialized(std::min(NumGoodPoints, static_cast<int32>(ceil(NumGoodPoints * Fraction))));
	for (int i = 0; i < CollisionVertices.Num(); i++)
	{
		CollisionVertices[i] = Vertices[Ordering[i]];
	}
	return CollisionVertices;
}

inline void CleanCollisionParticles(
	FTriangleMesh &TriMesh, 
	const TArrayView<const FVec3>& Vertices, 
	const FReal Fraction,
	TSet<int32>& ResultingIndices)
{
	ResultingIndices.Reset();
	if (Fraction <= 0.0)
		return;

	TArray<int32> CoincidentVertices;
	const TArray<int32> Ordering = TriMesh.GetVertexImportanceOrdering(Vertices, &CoincidentVertices, true);
	int32 NumGoodPoints = Ordering.Num() - CoincidentVertices.Num();
	NumGoodPoints = std::min(NumGoodPoints, static_cast<int32>(ceil(NumGoodPoints * Fraction)));

	ResultingIndices.Reserve(NumGoodPoints);
	for (int32 i = 0; i < NumGoodPoints; i++)
	{
		ResultingIndices.Add(Ordering[i]);
	}
}

template <typename T, int d>
using TClusterBuffer UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FClusterBuffer instead") = FClusterBuffer;

} // namespace Chaos
