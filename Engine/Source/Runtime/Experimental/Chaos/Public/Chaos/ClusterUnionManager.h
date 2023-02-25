// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ClusterCreationParameters.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

namespace Chaos
{
	using FClusterUnionIndex = int32;
	using FClusterUnionExplicitIndex = int32;

	class FRigidClustering;
	class FPBDRigidsEvolutionGBF;
	struct FClusterCreationParameters;

	enum class EClusterUnionOperation
	{
		Add,
		// AddReleased is the original behavior where if the particle to be added is a cluster, we will release the cluster first
		// and add its children instead.
		AddReleased
	};

	struct CHAOS_API FClusterUnion
	{
		// The root cluster particle that we created internally to represent the cluster.
		FPBDRigidClusteredParticleHandle* InternalCluster;

		// The thread-safe collision geometry that can be shared between the GT and PT.
		TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> SharedGeometry;

		// All the particles that belong to this cluster.
		TArray<FPBDRigidParticleHandle*> ChildParticles;

		// An explicit index set by the user if any.
		FClusterUnionExplicitIndex ExplicitIndex;

		// Need to remember the parameters used to create the cluster so we can update it later.
		FClusterCreationParameters Parameters;
	};

	/**
	 * This class is used by Chaos to create internal clusters that will
	 * cause one or more clusters to simulate together as a single rigid
	 * particle.
	 */
	class CHAOS_API FClusterUnionManager
	{
	public:
		FClusterUnionManager(FRigidClustering& InClustering, FPBDRigidsEvolutionGBF& InEvolution);

		// Creates a new cluster union with an automatically assigned cluster union index.
		FClusterUnionIndex CreateNewClusterUnion(const FClusterCreationParameters& Parameters, FClusterUnionExplicitIndex ExplicitIndex = INDEX_NONE, const FUniqueIdx* UniqueIndex = nullptr);

		// Destroy a given cluster union.
		void DestroyClusterUnion(FClusterUnionIndex Index);

		// Add a new operation to the queue.
		void AddPendingExplicitIndexOperation(FClusterUnionExplicitIndex Index, EClusterUnionOperation Op, const TArray<FPBDRigidParticleHandle*>& Particles);
		void AddPendingClusterIndexOperation(FClusterUnionIndex Index, EClusterUnionOperation Op, const TArray<FPBDRigidParticleHandle*>& Particles);

		// Will be called at the beginning of every time step to ensure that all the expected cluster unions have been modified.
		void FlushPendingOperations();

		// Access the cluster union externally.
		FClusterUnion* FindClusterUnionFromExplicitIndex(FClusterUnionExplicitIndex Index);
		FClusterUnion* FindClusterUnion(FClusterUnionIndex Index);

	private:
		FRigidClustering& MClustering;
		FPBDRigidsEvolutionGBF& MEvolution;

		using FClusterOpMap = TMap<EClusterUnionOperation, TArray<FPBDRigidParticleHandle*>>;
		template<typename TIndex>
		using TClusterIndexOpMap = TMap<TIndex, FClusterOpMap>;

		TClusterIndexOpMap<FClusterUnionIndex> PendingClusterIndexOperations;
		TClusterIndexOpMap<FClusterUnionExplicitIndex> PendingExplicitIndexOperations;

		template<typename TIndex>
		void AddPendingOperation(TClusterIndexOpMap<TIndex>& OpMap, TIndex Index, EClusterUnionOperation Op, const TArray<FPBDRigidParticleHandle*>& Particles)
		{
			FClusterOpMap& Ops = OpMap.FindOrAdd(Index);
			TArray<FPBDRigidParticleHandle*>& OpData = Ops.FindOrAdd(Op);
			OpData.Append(Particles);
		}

		// All of our actively managed cluster unions. We need to keep track of these
		// so a user could use the index to request modifications to a specific cluster union.
		TMap<FClusterUnionIndex, FClusterUnion> ClusterUnions;
		
		//
		// There are two ways we can pick a new union index:
		// - If a cluster union gets released/destroyed, that index can be reused.
		// - Otherwise, we use the NextAvailableUnionIndex which is just the max index we've seen + 1.
		//
		FClusterUnionIndex ClaimNextUnionIndex();
		TArray<FClusterUnionIndex> ReusableIndices;
		FClusterUnionIndex NextAvailableUnionIndex = 1;

		// Using the user's passed in FClusterUnionIndex may result in strange unexpected behavior if the 
		// user creates a cluster with a specified index. Thus we will map all explicitly requested indices
		// (i.e. an index that comes in via FClusterUnionOperationData for the first time) to an automatically
		// generated index (i.e one that would returned via CreateNewClusterUnion).
		TMap<FClusterUnionExplicitIndex, FClusterUnionIndex> ExplicitIndexMap;
		FClusterCreationParameters DefaultClusterCreationParameters() const;

		// If no cluster index is set but an explicit index is set, map the explicit index to a regular index.
		FClusterUnionIndex GetOrCreateClusterUnionIndexFromExplicitIndex(FClusterUnionExplicitIndex InIndex);

		// Forcefully recreate the shared geometry on a cluster. Potentially expensive so ideally should be used rarely.
		TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> ForceRecreateClusterUnionSharedGeometry(const FClusterUnion& Union);

		// Actually performs the change specified in the FClusterUnionOperationData structure.
		void HandleAddOperation(FClusterUnionIndex ClusterIndex, const TArray<FPBDRigidParticleHandle*>& Particles, bool bReleaseClustersFirst);
	};

}