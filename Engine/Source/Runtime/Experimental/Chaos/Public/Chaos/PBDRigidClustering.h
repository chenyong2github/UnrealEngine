// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidClusteredParticles.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/Transform.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/ExternalCollisionData.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/ClusterCreationParameters.h"
#include "Framework/BufferedData.h"

#define TODO_CONVERT_GEOMETRY_COLLECTION_PARTICLE_INDICES_TO_PARTICLE_POINTERS 0

namespace Chaos
{
	extern float ChaosClusteringChildrenInheritVelocity;
}

namespace Chaos
{

template <typename T, int d>
class CHAOS_API TClusterBuffer
{
public:
	using FClusterChildrenMap = TMap<TPBDRigidParticleHandle<T, d>*, TArray<TPBDRigidParticleHandle<T, d>*>>;
	using FClusterTransformMap = TMap<TPBDRigidParticleHandle<T, d>*, TRigidTransform<float, 3>>;

	virtual ~TClusterBuffer() = default;

	FClusterChildrenMap MChildren;
	FClusterTransformMap ClusterParentTransforms;
	TArray<Chaos::TSerializablePtr<FImplicitObject>> GeometryPtrs;
};

/* 
* PDBRigidClustering
*/
template<class T_FPBDRigidEvolution, class T_FPBDCollisionConstraint, class T, int d>
class CHAOS_API TPBDRigidClustering
{
	typedef typename T_FPBDCollisionConstraint::FPointContactConstraint FPointContactConstraint;
public:
	/** Parent to children */
	typedef TMap<TPBDRigidParticleHandle<T, d>*, TArray<TPBDRigidParticleHandle<T, d>*> > FClusterMap;

	using FCollisionConstraintHandle = FPBDCollisionConstraintHandle;

	TPBDRigidClustering(T_FPBDRigidEvolution& InEvolution, TPBDRigidClusteredParticles<T, d>& InParticles);
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
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* CreateClusterParticle(
		const int32 ClusterGroupIndex, 
		TArray<Chaos::TPBDRigidParticleHandle<T,d>*>&& Children, 
		const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>(),
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry = nullptr,
		const TRigidTransform<T, d>* ForceMassOrientation = nullptr);

	/**
	 *  CreateClusterParticleFromClusterChildren
	 *    Children : Rigid body ID to include in the cluster.
	 */
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* CreateClusterParticleFromClusterChildren(
		TArray<TPBDRigidParticleHandle<T,d>*>&& Children, 
		TPBDRigidClusteredParticleHandle<T,d>* Parent,
		const TRigidTransform<T, d>& ClusterWorldTM, 
		const FClusterCreationParameters<T>& Parameters/* = FClusterCreationParameters<T>()*/);

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
	TSet<TPBDRigidParticleHandle<T, d>*> DeactivateClusterParticle(TPBDRigidClusteredParticleHandle<T,d>* ClusteredParticle);

	/*
	*  ReleaseClusterParticles (BasedOnStrain)
	*    Release clusters based on the passed in \p ExternalStrainArray, or the 
	*    particle handle's current \c CollisionImpulses() value. Any cluster bodies 
	*    that have a strain value less than this valid will be released from the 
	*    cluster.
	*/
	TSet<TPBDRigidParticleHandle<T, d>*> ReleaseClusterParticles(
		TPBDRigidClusteredParticleHandle<T, d>* ClusteredParticle, 
		const TMap<TGeometryParticleHandle<T, d>*, float>* ExternalStrainMap = nullptr,
		bool bForceRelease = false);

	/*
	*  ReleaseClusterParticles
	*    Release all rigid body IDs passed,
	*/
	TSet<TPBDRigidParticleHandle<T, d>*> ReleaseClusterParticles(
		TArray<TPBDRigidParticleHandle<T, d>*> ChildrenParticles);

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
	void AdvanceClustering(const T dt, T_FPBDCollisionConstraint& CollisionRule);

	/**
	*  BreakingModel
	*    Implements the promotion breaking model, where strain impulses are
	*    summed onto the cluster body, and released if greater than the
	*    encoded strain. The remainder strains are propagated back down to
	*    the children clusters.
	*/
	TMap<TPBDRigidClusteredParticleHandle<T, d>*, TSet<TPBDRigidParticleHandle<T, d>*>> BreakingModel(
		TMap<TGeometryParticleHandle<T, d>*, float>* ExternalStrainMap = nullptr);

	/**
	*  PromoteStrains
	*    Sums the strains based on the cluster hierarchy. For example
	*    a cluster with two children that have strains {3,4} will have
	*    a ExternalStrain entry of 7. Will only decent the current
	*    node passed, and ignores the disabled flag.
	*/
	T PromoteStrains(TPBDRigidParticleHandle<T, d>* CurrentNode);

	/*
	*  Process the kinematic state of the clusters. Because the leaf node geometry can
	*  be changed by the solver, it is necessary to check all the sub clusters.
	*/
	void UpdateKinematicProperties(Chaos::TPBDRigidParticleHandle<float, 3>* Parent);

	//
	// Access
	//
	//  The ClusterIds and ChildrenMap are shared resources that can
	//  be accessed via the game thread.
	//
	const TClusterBuffer<T, d>&                         GetBufferedData() const { ResourceLock.ReadLock(); return BufferResource; } /* Secure access from game thread*/
	void                                                ReleaseBufferedData() const { ResourceLock.ReadUnlock(); }    /* Release access from game thread*/
	void                                                SwapBufferedData();                                         /* Managed by the PBDRigidSolver ONLY!*/


	/*
	*  GetActiveClusterIndex
	*    Get the current childs active cluster. Returns INDEX_NONE if
	*    not active or driven.
	*/
	TPBDRigidParticleHandle<T, d>* GetActiveClusterIndex(TPBDRigidParticleHandle<T, d>* Child);

	/*
	*  GetClusterIdsArray
	*    The cluster ids provide a mapping from the rigid body index
	*    to its parent cluster id. The parent id might not be the
	*    active id, see the GetActiveClusterIndex to find the active cluster.
	*    INDEX_NONE represents a non-clustered body.
	*/
	TArrayCollectionArray<ClusterId>&             GetClusterIdsArray() { return MParticles.ClusterIdsArray(); }
	const TArrayCollectionArray<ClusterId>&             GetClusterIdsArray() const { return MParticles.ClusterIdsArray(); }

	/*
	*  GetInternalClusterArray
	*    The internal cluster array indicates if this cluster was generated internally
	*    and would no be owned by an external source.
	*/
	const TArrayCollectionArray<bool>&                  GetInternalClusterArray() const { return MParticles.InternalClusterArray(); }

	/*
	*  GetChildToParentMap
	*    This map stores the relative transform from a child to its cluster parent.
	*/
	const TArrayCollectionArray<TRigidTransform<T, d>>& GetChildToParentMap() const { return MParticles.ChildToParentArray(); }

	/*
	*  GetStrainArray
	*    The strain array is used to store the maximum strain allowed for a individual
	*    body in the simulation. This attribute is initialized during the creation of
	*    the cluster body, can be updated during the evaluation of the simulation.
	*/
	TArrayCollectionArray<T>&                           GetStrainArray() { return MParticles.StrainsArray(); }

	/**
	*  GetParentToChildren
	*    The parent to children map stores the currently active cluster ids (Particle Indices) as
	*    the keys of the map. The value of the map is a pointer to an array  constrained
	*    rigid bodies.
	*/
	FClusterMap &										GetChildrenMap() { return MChildren; }
	const FClusterMap &                                 GetChildrenMap() const { return MChildren; }

	/*
	*  GetClusterGroupIndexArray
	*    The group index is used to automatically bind disjoint clusters. This attribute it set
	*    during the creation of cluster to a positive integer value. During UnionClusterGroups (which
	*    is called during AdvanceClustering) the positive bodies are joined with a negative pre-existing
	*    body, then set negative. Zero entries are ignored within the union.
	*/
	TArrayCollectionArray<int32>&                       GetClusterGroupIndexArray() { return MParticles.ClusterGroupIndexArray(); }

	/** Indicates if the child geometry is approximated by a single proxy */
	const TArrayCollectionArray<FMultiChildProxyId>& GetMultiChildProxyIdArray() const { return MParticles.MultiChildProxyIdArray(); }

	/** If multi child proxy is used, this is the data needed */
	const TArrayCollectionArray<TUniquePtr<TMultiChildProxyData<T, d>>>& GetMultiChildProxyDataArray() const { return MParticles.MultiChildProxyDataArray(); }

	void AddToClusterUnion(int32 ClusterID, TPBDRigidClusteredParticleHandle<T, 3>* Handle) {
		if (ClusterID <= 0) return;
		if (!ClusterUnionMap.Contains(ClusterID)) ClusterUnionMap.Add(ClusterID, TArray<TPBDRigidClusteredParticleHandle<T, 3>*>());
		ClusterUnionMap[ClusterID].Add(Handle);
	}

	const TArray<TBreakingData<float, 3>>& GetAllClusterBreakings() const { return MAllClusterBreakings; }
	void SetGenerateClusterBreaking(bool DoGenerate) { DoGenerateBreakingData = DoGenerate; }
	void ResetAllClusterBreakings() { MAllClusterBreakings.Reset(); }

	/*
	* GetConnectivityEdges
	*    Provides a list of each rigid body's current siblings and associated strain within the cluster.
	*/
	const TArrayCollectionArray<TArray<TConnectivityEdge<T>>>& GetConnectivityEdges() const { return MParticles.ConnectivityEdgesArray(); }

	/**
	* GenerateConnectionGraph
	*   Creates a connection graph for the given index using the creation parameters. This will not
	*   clear the existing graph.
	*/
	void SetClusterConnectionFactor(float ClusterConnectionFactorIn) { MClusterConnectionFactor = ClusterConnectionFactorIn; }
	void SetClusterUnionConnectionType(typename FClusterCreationParameters<T>::EConnectionMethod ClusterConnectionType) { MClusterUnionConnectionType = ClusterConnectionType; }

	void GenerateConnectionGraph(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T> & Parameters = FClusterCreationParameters<T>());

	const TSet<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& GetTopLevelClusterParents() const { return TopLevelClusterParents; }
	TSet<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& GetTopLevelClusterParents() { return TopLevelClusterParents; }

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
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent, 
		TSet<TPBDRigidParticleHandle<T, d>*>& Children, 
		const TRigidTransform<T, d>* ForceMassOrientation);
	void UpdateGeometry(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent, 
		const TSet<TPBDRigidParticleHandle<T, d>*>& Children, 
		TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> ProxyGeometry,
		const FClusterCreationParameters<T>& Parameters);

	void ComputeStrainFromCollision(const T_FPBDCollisionConstraint& CollisionRule);
	void ResetCollisionImpulseArray();
	void DisableCluster(TPBDRigidClusteredParticleHandle<T, d>* ClusteredParticle);
	void DisableParticleWithBreakEvent(Chaos::TPBDRigidParticleHandle<float, 3>* Particle);

	/*
	* Connectivity
	*/
	void UpdateConnectivityGraphUsingPointImplicit(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());
	void FixConnectivityGraphUsingDelaunayTriangulation(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());
	void UpdateConnectivityGraphUsingDelaunayTriangulation(
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
		const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());

	void ConnectNodes(
		TPBDRigidParticleHandle<T, d>* Child1,
		TPBDRigidParticleHandle<T, d>* Child2);
	void ConnectNodes(
		TPBDRigidClusteredParticleHandle<T, d>* Child1,
		TPBDRigidClusteredParticleHandle<T, d>* Child2);

	void RemoveNodeConnections(TPBDRigidParticleHandle<T, d>* Child);
	void RemoveNodeConnections(TPBDRigidClusteredParticleHandle<T, d>* Child);

private:

	T_FPBDRigidEvolution& MEvolution;
	TPBDRigidClusteredParticles<T, d>& MParticles;
	TSet<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*> TopLevelClusterParents;
	TSet<Chaos::TPBDRigidParticleHandle<float, 3>*> MActiveRemovalIndices;


	// Cluster data
	mutable FRWLock ResourceLock;
	TClusterBuffer<T, d> BufferResource;
	FClusterMap MChildren;
	TMap<int32, TArray<TPBDRigidClusteredParticleHandle<T, 3>*> > ClusterUnionMap;


	// Collision Impulses
	bool MCollisionImpulseArrayDirty;
	
	// Breaking data
	bool DoGenerateBreakingData;
	TArray<TBreakingData<float, 3>> MAllClusterBreakings;

	float MClusterConnectionFactor;
	typename FClusterCreationParameters<T>::EConnectionMethod MClusterUnionConnectionType;
};

template <typename T, int d>
void UpdateClusterMassProperties(
	Chaos::TPBDRigidClusteredParticleHandle<float, 3>* Parent,
	TSet<TPBDRigidParticleHandle<T, d>*>& Children,
	const TRigidTransform<T, d>* ForceMassOrientation = nullptr);

template<typename T, int d>
TArray<TVector<T, d>> CleanCollisionParticles(
	const TArray<TVector<T, d>>& Vertices, 
	TAABB<T, d> BBox, 
	const float SnapDistance=0.01)
{
	const int32 NumPoints = Vertices.Num();
	if (NumPoints <= 1)
		return TArray<TVector<T, d>>(Vertices);

	T MaxBBoxDim = BBox.Extents().Max();
	if (MaxBBoxDim < SnapDistance)
		return TArray<TVector<T, d>>(&Vertices[0], 1);

	BBox.Thicken(FMath::Max(SnapDistance/10, KINDA_SMALL_NUMBER*10)); // 0.001
	MaxBBoxDim = BBox.Extents().Max();

	const TVector<T, d> PointsCenter = BBox.Center();
	TArray<TVector<T, d>> Points(Vertices);

	// Find coincident vertices.  We hash to a grid of fine enough resolution such
	// that if 2 particles hash to the same cell, then we're going to consider them
	// coincident.
	TSet<int64> OccupiedCells;
	OccupiedCells.Reserve(NumPoints);

	TArray<int32> Redundant;
	Redundant.Reserve(NumPoints); // Excessive, but ensures consistent performance.

	int32 NumCoincident = 0;
	const int64 Resolution = static_cast<int64>(floor(MaxBBoxDim / FMath::Max(SnapDistance,KINDA_SMALL_NUMBER)));
	const T CellSize = MaxBBoxDim / Resolution;
	for (int32 i = 0; i < 2; i++)
	{
		Redundant.Reset();
		OccupiedCells.Reset();
		// Shift the grid by 1/2 a grid cell the second iteration so that
		// we don't miss slightly adjacent coincident points across cell
		// boundaries.
		const TVector<T, 3> GridCenter = TVector<T, 3>(0) - TVector<T, 3>(i * CellSize / 2);
		for (int32 j = 0; j < Points.Num(); j++)
		{
			const TVector<T, 3> Pos = Points[j] - PointsCenter; // Centered at the origin
			const TVector<int64, 3> Coord(
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

template<typename T, int d>
TArray<TVector<T, d>> CleanCollisionParticles(
	const TArray<TVector<T, d>>& Vertices, 
	const float SnapDistance=0.01)
{
	if (!Vertices.Num())
	{
		return TArray<TVector<T, d>>();
	}
	TAABB<T, d> BBox(TAABB<T, d>::EmptyAABB());
	for (const TVector<T, d>& Pt : Vertices)
	{
		BBox.GrowToInclude(Pt);
	}
	return CleanCollisionParticles(Vertices, BBox, SnapDistance);
}

template <typename T, int d>
TArray<TVector<T,d>> 
CleanCollisionParticles(
	TTriangleMesh<T> &TriMesh, 
	const TArrayView<const TVector<T,d>>& Vertices, 
	const float Fraction)
{
	TArray<TVector<T, d>> CollisionVertices;
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

template <typename T, int d>
void
CleanCollisionParticles(
	TTriangleMesh<T> &TriMesh, 
	const TArrayView<const TVector<T, d>>& Vertices, 
	const float Fraction, 
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

} // namespace Chaos
