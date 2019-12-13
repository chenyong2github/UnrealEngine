// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidClusteredParticles.h"
#include "Chaos/Transform.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/ExternalCollisionData.h"
#include "Chaos/TriangleMesh.h"
#include "Chaos/ClusterCreationParameters.h"
#include "Framework/BufferedData.h"

#define TODO_CONVERT_GEOMETRY_COLLECTION_PARTICLE_INDICES_TO_PARTICLE_POINTERS 0

namespace Chaos
{

template <typename T, int d>
class CHAOS_API TClusterBuffer
{
public:
	using FClusterChildrenMap = TMap<uint32, TArray<uint32>>;
	using FClusterTransformMap = TMap<uint32, TRigidTransform<float, 3>>;

	virtual ~TClusterBuffer() = default;

	FClusterChildrenMap MChildren;
	FClusterTransformMap ClusterParentTransforms;
	TArray<Chaos::TSerializablePtr<FImplicitObject>> GeometryPtrs;
};

/* 
* PDBRigidClustering
*/
template<class FPBDRigidEvolution, class FPBDCollisionConstraint, class T, int d>
class CHAOS_API TPBDRigidClustering
{
	typedef typename FPBDCollisionConstraint::FPointContactConstraint FPointContactConstraint;

public:
	typedef TMap<const TGeometryParticleHandle<T,d>*, TArray<uint32> > FClusterMap;
	using FCollisionConstraintHandle = TPBDCollisionConstraintHandle<T, d>;

	TPBDRigidClustering(FPBDRigidEvolution& InEvolution, TPBDRigidClusteredParticles<T, d>& InParticles);
	~TPBDRigidClustering();

	//
	// Initialization
	//

	/*
	*  Create Cluster : Initialize clusters in the simulation.
	*    ClusterGroupIndex : Index to join cluster into.
	*    Children : Rigid body ID to include in the cluster.
	*    ProxyGeometry : Collision default for the cluster, automatically generated otherwise.
	*    ForceMassOrientation : Inertial alignment into mass space.
	*/
	int32 CreateClusterParticle(int32 ClusterGroupIndex, TArray<uint32>&& Children,
		TSerializablePtr<FImplicitObject> ProxyGeometry = TSerializablePtr<FImplicitObject>(),
		const TRigidTransform<T, d>* ForceMassOrientation = nullptr, const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());

	/*
	*  CreateClusterParticleFromClusterChildren
	*    Children : Rigid body ID to include in the cluster.
	*/
	int32 CreateClusterParticleFromClusterChildren(TArray<uint32>&& Children, TPBDRigidClusteredParticleHandle<T,d>* Parent,
		const TRigidTransform<T, d>& ClusterWorldTM, const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());

	/*
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
	TSet<uint32> DeactivateClusterParticle(TPBDRigidClusteredParticleHandle<T,d>* ClusteredParticle);


	/*
	*  ReleaseClusterParticles (BasedOnStrain)
	*    Release clusters based on the passed in strains. Any cluster bodies that
	*    have a MStrain value less than its entry in the StrainArray will be
	*    released from the cluster.
	*/
	TSet<uint32> ReleaseClusterParticles(TPBDRigidClusteredParticleHandle<T, d>* ClusteredParticle, const TArrayView<T>& StrainArray);

	/*
	*  ReleaseClusterParticles
	*    Release all rigid body IDs passed,
	*/
	TSet<uint32> ReleaseClusterParticles(const TArray<uint32>& ClusteredParticles);


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
	void AdvanceClustering(const T dt, FPBDCollisionConstraint& CollisionRule);

	/**
	*  BreakingModel
	*    Implements the promotion breaking model, where strain impulses are
	*    summed onto the cluster body, and released if greater than the
	*    encoded strain. The remainder strains are propagated back down to
	*    the children clusters.
	*/
	TMap<TPBDRigidClusteredParticleHandle<T, d>*, TSet<uint32>> BreakingModel(TArrayView<T> ExternalStrain);

	/**
	*  PromoteStrains
	*    Sums the strains based on the cluster hierarchy. For example
	*    a cluster with two children that have strains {3,4} will have
	*    a ExternalStrain entry of 7. Will only decent the current
	*    node passed, and ignores the disabled flag.
	*/
	T PromoteStrains(uint32 CurrentNode, TArrayView<T>& ExternalStrains);

	/*
	*  Process the kinematic state of the clusters. Because the leaf node geometry can
	*  be changed by the solver, it is necessary to check all the sub clusters.
	*/
	void UpdateKinematicProperties(uint32 ClusterIndex);


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
	int32                                               GetActiveClusterIndex(uint32 ChildIndex);

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
	*    The parent to children map stores the currently active cluster ids ( Particle Indices) as
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

	/*
	*  GetClusterGroupIndexArray
	*  Cluster counters are used the defer the initialization of grouped clusters until
	*  all bodies are initialized within the simulation. During construction the counter
	*  is incremented, and when ActivateBodies on the solver triggers activation across
	*  multiple sets of bodies, the counter is decremented, when its back to Zero the
	*  the union cluster is allowed to initialize. Once a cluster group ID is used up
	*  is can not be reused.
	*/
	void IncrementPendingClusterCounter(uint32 ClusterGroupID);
	void DecrementPendingClusterCounter(uint32 ClusterGroupID);
	int32 NumberOfPendingClusters() const { return PendingClusterCounter.Num(); }

	const TArray<TBreakingData<float, 3>>& GetAllClusterBreakings() const { return MAllClusterBreakings; }
	void SetGenerateClusterBreaking(bool DoGenerate) { DoGenerateBreakingData = DoGenerate; }
	void ResetAllClusterBreakings() { MAllClusterBreakings.Reset(); }

	/*
	* GetConnectivityEdges
	*    Provides a list of each rigid body's current siblings and associated strain within the cluster.
	*/
//#if CHAOS_PARTICLEHANDLE_TODO: is this really needed?
	const TArrayCollectionArray<TArray<TConnectivityEdge<T>>>& GetConnectivityEdges() const { return MParticles.ConnectivityEdgesArray(); }
//#endif

	/**
	* GenerateConnectionGraph
	*   Creates a connection graph for the given index using the creation parameters. This will not
	*   clear the existing graph.
	*/
	void SetClusterConnectionFactor(float ClusterConnectionFactorIn) { MClusterConnectionFactor = ClusterConnectionFactorIn; }
	void SetClusterUnionConnectionType(typename FClusterCreationParameters<T>::EConnectionMethod ClusterConnectionType) { MClusterUnionConnectionType = ClusterConnectionType; }
	void GenerateConnectionGraph(int32 NewIndex, const FClusterCreationParameters<T> & Parameters = FClusterCreationParameters<T>());
	const TSet<int32>& GetTopLevelClusterParents() const { return TopLevelClusterParents; }
	TSet<int32>& GetTopLevelClusterParents() { return TopLevelClusterParents; }

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



 protected:

	void UpdateMassProperties(const TArray<uint32>& Children, const uint32 NewIndex, const TRigidTransform<T, d>* ForceMassOrientation);
	void UpdateGeometry(const TArray<uint32>& Children, const uint32 NewIndex, TSerializablePtr<FImplicitObject> ProxyGeometry, const FClusterCreationParameters<T>& Parameters);
	void ComputeStrainFromCollision(const FPBDCollisionConstraint& CollisionRule);
	void ResetCollisionImpulseArray();
	void DisableCluster(TPBDRigidClusteredParticleHandle<T, d>* ClusteredParticle);
	void DisableParticleWithBreakEvent(uint32 ClusterIndex);

	/*
	* Connectivity
	*/
	void UpdateConnectivityGraphUsingPointImplicit(uint32 ClusterIndex, const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());
	void FixConnectivityGraphUsingDelaunayTriangulation(uint32 ClusterIndex, const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());
	void UpdateConnectivityGraphUsingDelaunayTriangulation(uint32 ClusterIndex, const FClusterCreationParameters<T>& Parameters = FClusterCreationParameters<T>());
	void AddUniqueConnection(uint32 Index1, uint32 Index2, T Strain);
	void ConnectNodes(uint32 Index1, uint32 Index2, T Strain);
	void RemoveNodeConnections(uint32 ParticleIndex);

private:

	FPBDRigidEvolution& MEvolution;
	TPBDRigidClusteredParticles<T, d>& MParticles;
	TSet<int32> TopLevelClusterParents;
	TSet<int32> MActiveRemovalIndices;

	// Cluster data
	mutable FRWLock ResourceLock;
	TClusterBuffer<T, d> BufferResource;
	FClusterMap MChildren;
	TMap<int32, int32> PendingClusterCounter;


	// Collision Impulses
	bool MCollisionImpulseArrayDirty;
	
	// Breaking data
	bool DoGenerateBreakingData;
	TArray<TBreakingData<float, 3>> MAllClusterBreakings;

	float MClusterConnectionFactor;
	typename FClusterCreationParameters<T>::EConnectionMethod MClusterUnionConnectionType;
};

template <typename T, int d>
void UpdateClusterMassProperties(TPBDRigidClusteredParticles<T, d>& Particles, const TArray<uint32>& Children,
	const uint32 NewIndex, const TRigidTransform<T, d>* ForceMassOrientation = nullptr,
	const TArrayCollectionArray<TUniquePtr<TMultiChildProxyData<T, d>>>* MMultiChildProxyData = nullptr,
	const TArrayCollectionArray<FMultiChildProxyId>* MMultiChildProxyId = nullptr);

template<typename T, int d>
TArray<TVector<T, d>> CleanCollisionParticles(const TArray<TVector<T, d>>& Vertices, TAABB<T, d> BBox, const float SnapDistance=0.01)
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
TArray<TVector<T, d>> CleanCollisionParticles(const TArray<TVector<T, d>>& Vertices, const float SnapDistance=0.01)
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
CleanCollisionParticles(TTriangleMesh<T> &TriMesh, const TArrayView<const TVector<T,d>>& Vertices, float Fraction)
{
	TArray<TVector<T, d>> CollisionVertices;
	if (Fraction <= 0.0)
		return CollisionVertices;

	// Get the importance vertex ordering, from most to least.  Reorder the 
	// particles accordingly.
	TArray<int32> CoincidentVertices;
	const TArray<int32> Ordering = TriMesh.GetVertexImportanceOrdering(Vertices, &CoincidentVertices, true);

	// Particles are ordered from most important to least, with coincident 
	// vertices at the very end.
	const int32 numGoodPoints = Ordering.Num() - CoincidentVertices.Num();

#if DO_GUARD_SLOW
	for (int i = numGoodPoints; i < Ordering.Num(); ++i)
	{
		ensure(CoincidentVertices.Contains(Ordering[i]));	//make sure all coincident vertices are at the back
	}
#endif

	CollisionVertices.AddUninitialized(std::min(numGoodPoints, static_cast<int32>(ceil(numGoodPoints * Fraction))));
	for(int i=0; i < CollisionVertices.Num(); i++)
		CollisionVertices[i] = Vertices[Ordering[i]];

	return CollisionVertices;
}


template <typename T, int d>
void
CleanCollisionParticles( /*const*/ TTriangleMesh<T> &TriMesh, const TArrayView<const TVector<T, d>>& Vertices, float Fraction, TSet<int32> & ResultingIndices)
{
	if (Fraction <= 0.0)
		return;

	TArray<int32> CoincidentVertices;
	const TArray<int32> Ordering = TriMesh.GetVertexImportanceOrdering(Vertices, &CoincidentVertices, true);
	const int32 NumGoodPoints = Ordering.Num() - CoincidentVertices.Num();

	ResultingIndices.Reserve(NumGoodPoints);
	for (int32 i = 0; i < NumGoodPoints; i++)
	{
		ResultingIndices.Add(Ordering[i]);
	}
}



} // namespace Chaos
