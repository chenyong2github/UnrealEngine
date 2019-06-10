// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionTypes.h"


// @todo(ccaulfield): can we get rid of this now?
#define USE_CONTACT_LEVELS 1

namespace Chaos
{
template<typename T, int d>
class TPBDRigidParticles;

template<typename T, int d>
class TPBDConstraintColor;

template<class T>
class TChaosPhysicsMaterial;

template <typename T>
class TSerializablePtr;

template<typename T>
class TArrayCollectionArray;


/**
 * Build a graph of connected particles, and then a set of independent islands.
 * Particles/constraints in different islands do not interact, so islands can be updated in parallel.
 * This is also where particle sleeping is controlled.
 */
// @todo(ccaulfield): Break out the Island and Sleep stuff from the particle graph
// @todo(ccaulfield): The level system should be part of the Island system, and not embedded with the color system
template<typename T, int d>
class CHAOS_API TPBDConstraintGraph
{
public:
	friend class TPBDConstraintColor<T, d>;

	/** Number of sleep threshold passes before sleep mode is enabled */
	static const int32 SleepCountThreshold = 5;

	/** Information required to map a graph edge back to its constraint */
	struct FConstraintData
	{
		uint32 ContainerId;
		int32 ConstraintIndex;
	};

	TPBDConstraintGraph();
	TPBDConstraintGraph(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InActiveIndices);
	virtual ~TPBDConstraintGraph() {}

	/** 
	 * Set up the particle-to-graph-node mapping for the specified active particle indices, and clear the graph.
	 * Should be called before AddConstraint.
	 */
	void InitializeGraph(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices);

	/**
	 * Reserve space in the graph for NumConstraints additional constraints.
	 */
	void ReserveConstraints(const int32 NumConstraints);

	/**
	 * Add a constraint to the graph for each constraint in the container.
	 */
	void AddConstraint(const uint32 InContainerId, const int32 InConstraintIndex, const TVector<int32, 2>& InConstrainedParticles);

	/**
	 * Add particles/constraints to their particle's already-assigned islands (if applicable).
	 */
	// @todo(ccaulfield): InitializeIslands and ResetIslands are a bit confusing. Try to come up with better names.
	void ResetIslands(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices);

	/**
	 * Generate the simulation islands of connected particles (AddConstraints must have already been called). There are no constraints connecting particles in different islands.
	 */
	void UpdateIslands(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, TSet<int32>& ActiveIndices);

	/**
	 * Put particles in inactive islands to sleep.
	 */
	bool SleepInactive(TPBDRigidParticles<T, d>& InParticles, const int32 Island, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterialAttributes);

	/**
	 * Wake all particles in an Island.
	 */
	void WakeIsland(TPBDRigidParticles<T, d>& InParticles, const int32 Island);

	/**
	 * Ensure that the particles in each island have consistent sleep states - if any are awake, wake all.
	 */
	// @todo(ccaulfield): Do we really need this? It implies some behind-the-scenes state manipulation.
	void ReconcileIslands(TPBDRigidParticles<T, d>& InParticles);

	/**
	 * Get the list of ConstraintData indices associated with the specified island. NOTE: ConstraintDataIndex != ConstraintIndex.
	 * Indices returned are into the ConstraintData array in the ConstraintGraph to get to the Constraint Index and Container Id
	 * @see GetConstraintsData().
	 */
	const TArray<int32>& GetIslandConstraintData(int32 Island) const
	{
		return IslandConstraints[Island];
	}

	/**
	 * Get the list of Particle indices associated with the specified island.
	 */
	const TArray<int32>& GetIslandParticles(int32 Island) const
	{
		return IslandParticles[Island];
	}

	/**
	 * Get the list of Particle indices associated with the specified island.
	 */
	int32 GetIslandSleepCount(int32 Island) const
	{
		return IslandSleepCounts[Island];
	}

	/**
	 * The number of islands in the graph.
	 */
	int32 NumIslands() const
	{
		return IslandParticles.Num();
	}

	/**
	 * Information mapping the edge back to a rule's constraint
	 */
	const FConstraintData& GetConstraintData(int32 ConstraintDataIndex) const;

	/**
	 * Enable a particle after initialization. Adds the particle to the island of the parent particle if supplied.
	 */
	void EnableParticle(TPBDRigidParticles<T, d>& InParticles, const int32 ParticleIndex, const int32 ParentParticleIndex);

	/**
	 * Disable a particle and remove it from its island.
	 * @note: this does not remove any attached constraints - constraints need to be re-added and islands will need to be rebuilt.
	 */
	void DisableParticle(TPBDRigidParticles<T, d>& InParticles, const int32 ParticleIndex);

	/**
	 * Disable a set of particles and remove them from their island.
	 * @note: this does not remove any attached constraints - constraints need to be re-added and islands will need to be rebuilt.
	 */
	void DisableParticles(TPBDRigidParticles<T, d>& InParticles, const TSet<int32>& InParticleIndices);

private:
	struct FGraphNode
	{
		FGraphNode()
			: BodyIndex(INDEX_NONE)
			, Island(INDEX_NONE)
		{
		}

		TArray<int32> Edges;
		int32 BodyIndex;
		int32 Island;
	};

	struct FGraphEdge
	{
		FGraphEdge()
			: FirstNode(INDEX_NONE)
			, SecondNode(INDEX_NONE)
		{
		}

		int32 FirstNode;
		int32 SecondNode;
		FConstraintData Data;
	};

	struct FIslandData
	{
		FIslandData()
			: bIsIslandPersistant(false)
		{
		}

		bool bIsIslandPersistant;
	};

	void ComputeIslands(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, TSet<int32>& ActiveIndices);
	void ComputeIsland(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const int32 Node, const int32 Island, TSet<int32>& DynamicParticlesInIsland, TSet<int32>& StaticParticlesInIsland);
	bool CheckIslands(TPBDRigidParticles<T, d>& InParticles, const TSet<int32>& InParticleIndices);

	TArray<FGraphNode> Nodes;
	TArray<FGraphEdge> Edges;
	TArray<FIslandData> IslandData;
	TMap<int32, int32> ParticleToNodeIndex;

	TArray<TArray<int32>> IslandParticles;
	TArray<TArray<int32>> IslandConstraints;
	TArray<int32> IslandSleepCounts;
};

}
