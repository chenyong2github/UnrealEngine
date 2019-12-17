// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ParticleHandle.h"


// @todo(ccaulfield): can we get rid of this now?
#define USE_CONTACT_LEVELS 1

namespace Chaos
{
	template<typename T, int d>
	class TPBDRigidParticles;

	template<typename T, int d>
	class TPBDConstraintColor;

	class FConstraintHandle;

	template<class T>
	class TChaosPhysicsMaterial;

	template <typename T>
	class TSerializablePtr;

	template<typename T>
	class TArrayCollectionArray;

	template <typename T, int d>
	class TPBDRigidsSOAs;

	/**
	 * Build a graph of connected particles, and then a set of independent islands.
	 * Particles/constraints in different islands do not interact, so islands can be updated in parallel.
	 * This is also where particle sleeping is controlled.
	 * Note that at the moment this graph is rebuilt every frame. When this changes some of the underlying data will need to be changed. I.e. move from indices to pointers to handles
	 */
	 // @todo(ccaulfield): Break out the Island and Sleep stuff from the particle graph
	 // @todo(ccaulfield): The level system should be part of the Island system, and not embedded with the color system
	class CHAOS_API FPBDConstraintGraph
	{
	public:
		friend class FPBDConstraintColor;

		/** Number of sleep threshold passes before sleep mode is enabled */
		static const int32 SleepCountThreshold = 5;

		/** Information required to map a graph edge back to its constraint */
		struct FConstraintData
		{
		public:
			FConstraintData() : ContainerId(0), ConstraintHandle(nullptr) {}
			FConstraintData(uint32 InContainerId, FConstraintHandle* InConstraintHandle) : ContainerId(InContainerId), ConstraintHandle(InConstraintHandle) {}

			uint32 GetContainerId() const { return ContainerId; }
			FConstraintHandle* GetConstraintHandle() const { return ConstraintHandle; }

		private:
			uint32 ContainerId;
			FConstraintHandle* ConstraintHandle;
		};

		FPBDConstraintGraph();
		FPBDConstraintGraph(const TParticleView<TGeometryParticles<FReal,3>>& Particles);
		virtual ~FPBDConstraintGraph() {}

		/**
		 * Clear the graph and set up the particle-to-graph-node mapping for the specified particles
		 * Should be called before AddConstraint.
		 */
		void InitializeGraph(const TParticleView<TGeometryParticles<FReal, 3>>& Particles);

		/**
		 * Reserve space in the graph for NumConstraints additional constraints.
		 */
		void ReserveConstraints(const int32 NumConstraints);

		/**
		 * Add a constraint to the graph for each constraint in the container.
		 */
		void AddConstraint(const uint32 InContainerId, FConstraintHandle* InConstraintHandle, const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& InConstrainedParticles);

		/**
		 * Add particles/constraints to their particle's already-assigned islands (if applicable).
		 */
		 // @todo(ccaulfield): InitializeIslands and ResetIslands are a bit confusing. Try to come up with better names.
		void ResetIslands(const TParticleView<TPBDRigidParticles<FReal, 3>>& PBDRigids);

		/**
		 * Generate the simulation islands of connected particles (AddConstraints must have already been called). There are no constraints connecting particles in different islands.
		 */
		void UpdateIslands(const TParticleView<TPBDRigidParticles<FReal, 3>>& confusing, TPBDRigidsSOAs<FReal, 3>& Particles);

		/**
		 * Put particles in inactive islands to sleep.
		 */
		bool SleepInactive(const int32 Island, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterialAttributes);

		/**
		 * Wake all particles in an Island.
		 */
		void WakeIsland(const int32 Island);

		/**
		 * Ensure that the particles in each island have consistent sleep states - if any are awake, wake all.
		 */
		 // @todo(ccaulfield): Do we really need this? It implies some behind-the-scenes state manipulation.
		void ReconcileIslands();

		/**
		 * Get the list of ConstraintsData indices associated with the specified island. NOTE: ConstraintDataIndex is an internal index and not related to 
		 * a constraint's index in its owning container. Indices are into the ConstraintData array in the ConstraintGraph to get to the Constraint Index and Container Id.
		 * @see GetConstraintsData().
		 */
		const TArray<int32>& GetIslandConstraintData(int32 Island) const
		{
			return IslandToConstraints[Island];
		}

		/**
		 * Get the list of Particle indices associated with the specified island.
		 */
		const TArray<TGeometryParticleHandle<FReal, 3>*>& GetIslandParticles(int32 Island) const
		{
			return IslandToParticles[Island];
		}

		/**
		 * Get the list of Particle indices associated with the specified island.
		 */
		int32 GetIslandSleepCount(int32 Island) const
		{
			return IslandToSleepCount[Island];
		}

		/**
		 * The number of islands in the graph.
		 */
		int32 NumIslands() const
		{
			return IslandToParticles.Num();
		}

		/**
		 * Information mapping the edge back to a rule's constraint
		 */
		const FConstraintData& GetConstraintData(int32 ConstraintDataIndex) const;

		/**
		 * Enable a particle after initialization. Adds the particle to the island of the parent particle if supplied.
		 */
		void EnableParticle(TGeometryParticleHandle<FReal, 3>* Particle, const TGeometryParticleHandle<FReal, 3>* ParentParticle);

		/**
		 * Disable a particle and remove it from its island.
		 * @note: this does not remove any attached constraints - constraints need to be re-added and islands will need to be rebuilt.
		 */
		void DisableParticle(TGeometryParticleHandle<FReal, 3>* Particle);

		void AddParticle(TGeometryParticleHandle<FReal, 3>* AddedParticle)
		{
			ParticleAdd(AddedParticle);
		}

		//Remove particle from constraint, maybe rethink some of these names
		void RemoveParticle(TGeometryParticleHandle<FReal, 3>* Particle)
		{
			//we know it just removes, probably rename later
			DisableParticle(Particle);
		}

		/**
		 * Disable a set of particles and remove them from their island.
		 * @note: this does not remove any attached constraints - constraints need to be re-added and islands will need to be rebuilt.
		 */
		void DisableParticles(const TSet<TGeometryParticleHandle<FReal, 3>*>& Particles);

	private:
		struct FGraphNode
		{
			FGraphNode()
				: Particle(nullptr)
				, Island(INDEX_NONE)
			{
			}

			TArray<int32> Edges;
			TGeometryParticleHandle<FReal, 3>* Particle;
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

		void ComputeIslands(const TParticleView<TPBDRigidParticles<FReal, 3>>& PBDRigids, TPBDRigidsSOAs<FReal, 3>& Particles);
		void ComputeIsland(const int32 Node, const int32 Island,
			TSet<TGeometryParticleHandle<FReal, 3>*>& DynamicParticlesInIsland, TSet<TGeometryParticleHandle<FReal, 3>*>& StaticParticlesInIsland);
		bool CheckIslands(const TArray<TGeometryParticleHandle<FReal, 3>*>& Particles);
		
		void ParticleAdd(TGeometryParticleHandle<FReal, 3>* AddedParticle);
		void ParticleRemove(TGeometryParticleHandle<FReal, 3>* RemovedParticle);
		int32 GetNextNodeIndex();
		const TArray<int32>& GetUpdatedNodes() const { return UpdatedNodes; }

		TArray<FGraphNode> Nodes;
		TArray<FGraphEdge> Edges;
		TArray<FIslandData> IslandToData;
		TMap<TGeometryParticleHandle<FReal, 3>*, int32> ParticleToNodeIndex;

		TArray<TArray<TGeometryParticleHandle<FReal, 3>*>> IslandToParticles;
		TArray<TArray<int32>> IslandToConstraints;
		TArray<int32> IslandToSleepCount;

		TArray<int32> FreeIndexList;
		TArray<int32> UpdatedNodes;
		TArray<int32> Visited;
		uint8 VisitToken;
	};

}
