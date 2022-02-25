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

namespace Chaos
{
	class FPBDCollisionConstraints;

struct CHAOS_API FClusterDestoryParameters {
	bool bReturnInternalOnly : true;
};

/****
*
*   FRigidClustering
* 
*   The Chaos Destruction System allows artists to define exactly how geometry 
*   will break and separate during the simulations. Artists construct the 
*	simulation assets using pre-fractured geometry and utilize dynamically 
*   generated rigid constraints to model the structural connections during the
*   simulation. The resulting objects within the simulation can separate from 
*   connected structures based on interactions with environmental elements, 
*   like fields and collisions.
*
*   The destruction system relies on an internal clustering model 
*   (aka Clustering) which controls how the rigidly attached geometry is 
*   simulated. Clustering allows artists to initialize sets of geometry as 
*   a single rigid body, then dynamically break the objects during the 
*   simulation. At its core, the clustering system will simply join the mass
*   and inertia of each connected element into one larger single rigid body.
* 
*   At the beginning of the simulation a connection graph is initialized 
*   based on the rigid body’s nearest neighbors. Each connection between the
*   bodies represents a rigid constraint within the cluster and is given 
*   initial strain values. During the simulation, the strains within the 
*   connection graph are evaluated. The connections can be broken when collision
*   constraints, or field evaluations, impart an impulse on the rigid body that
*   exceeds the connections limit. Fields can also be used to decrease the 
*   internal strain values of the connections, resulting in a weakening of the
*   internal structure
*
*/
class CHAOS_API FRigidClustering
{
public:

	typedef FPBDRigidsEvolutionGBF								FRigidEvolution;
	typedef FPBDRigidParticleHandle*							FRigidHandle;
	typedef TArray<FRigidHandle>								FRigidHandleArray;
	typedef FPBDRigidClusteredParticleHandle*					FClusterHandle;
	typedef TMap<FClusterHandle, FRigidHandleArray>				FClusterMap;
	typedef TFunction<void(FRigidClustering&, FRigidHandle)>	FVisitorFunction;

	FRigidClustering(FRigidEvolution& InEvolution, FPBDRigidClusteredParticles& InParticles);
	~FRigidClustering();

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
		const TMap<FGeometryParticleHandle*, Chaos::FReal>* ExternalStrainMap = nullptr,
		bool bForceRelease = false);

	TSet<FPBDRigidParticleHandle*> ReleaseClusterParticlesNoInternalCluster(
		FPBDRigidClusteredParticleHandle* ClusteredParticle,
		const TMap<FGeometryParticleHandle*, Chaos::FReal>* ExternalStrainMap = nullptr,
		bool bForceRelease = false);

	/*
	*  ReleaseClusterParticles
	*    Release all rigid body IDs passed,
	*/
	TSet<FPBDRigidParticleHandle*> ReleaseClusterParticles(
		TArray<FPBDRigidParticleHandle*> ChildrenParticles);

	/*
	*  DestroyClusterParticle
	*    Disable the cluster particle and remove from all internal clustering
	*    structures. This will not activate children.
	* 
	*    Returns the active parent cluster that might need to be rebuilt because
	*    its geometry might be pointing to deleted particle handels. 
	*/
	FPBDRigidClusteredParticleHandle* DestroyClusterParticle(
		FPBDRigidClusteredParticleHandle* ClusteredParticle,
		const FClusterDestoryParameters& Parameters = FClusterDestoryParameters());

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
	void AdvanceClustering(const FReal dt, FPBDCollisionConstraints& CollisionRule);

	/**
	*  BreakingModel
	*    Implements the promotion breaking model, where strain impulses are
	*    summed onto the cluster body, and released if greater than the
	*    encoded strain. The remainder strains are propagated back down to
	*    the children clusters.
	*/
	TMap<FPBDRigidClusteredParticleHandle*, TSet<FPBDRigidParticleHandle*>> BreakingModel(
		TMap<FGeometryParticleHandle*, Chaos::FReal>* ExternalStrainMap = nullptr);

	//
	// Access
	//

	/**
	*
	*  Visitor
	*   Walk all the decendents of the current cluster and execute FVisitorFunction.
	*   FVisitorFunction = [](FRigidClustering& Clustering, FRigidHandle RigidHandle){}
	*/
	void Visitor(FClusterHandle Cluster, FVisitorFunction Function);

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
	TArrayCollectionArray<ClusterId>& GetClusterIdsArray() { return MParticles.ClusterIdsArray(); }
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
	const TArrayCollectionArray<FReal>& GetStrainArray() const { return MParticles.StrainsArray(); }
		
	/**
	*  GetParentToChildren
	*    The parent to children map stores the currently active cluster ids (Particle Indices) as
	*    the keys of the map. The value of the map is a pointer to an array  constrained
	*    rigid bodies.
	*/
	FClusterMap & GetChildrenMap() { return MChildren; }
	const FClusterMap & GetChildrenMap() const { return MChildren; }

	/*
	*  GetClusterGroupIndexArray
	*    The group index is used to automatically bind disjoint clusters. This attribute it set
	*    during the creation of cluster to a positive integer value. During UnionClusterGroups (which
	*    is called during AdvanceClustering) the positive bodies are joined with a negative pre-existing
	*    body, then set negative. Zero entries are ignored within the union.
	*/
	TArrayCollectionArray<int32>& GetClusterGroupIndexArray() { return MParticles.ClusterGroupIndexArray(); }

	/*
	*  Cluster Break Data
	*     The cluster breaks can be used to seed particle emissions. 
	*/
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

	FRigidEvolution& GetEvolution() { return MEvolution; }
	const FRigidEvolution& GetEvolution() const { return MEvolution; }


 protected:

	void ComputeStrainFromCollision(const FPBDCollisionConstraints& CollisionRule);
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

	FRigidEvolution& MEvolution;
	FPBDRigidClusteredParticles& MParticles;
	TSet<Chaos::FPBDRigidClusteredParticleHandle*> TopLevelClusterParents;
	TSet<Chaos::FPBDRigidParticleHandle*> MActiveRemovalIndices;


	// Cluster data
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

} // namespace Chaos
