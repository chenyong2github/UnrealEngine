// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionTypes.h"
#include "ParticleHandle.h"


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
	template<typename T, int d>
	class CHAOS_API TPBDConstraintGraph
	{
	public:
		template <typename T2, int d2>
		friend class TPBDConstraintColor;

		/** Number of sleep threshold passes before sleep mode is enabled */
		static const int32 SleepCountThreshold = 5;

		/** Information required to map a graph edge back to its constraint */
		struct FConstraintData
		{
			uint32 ContainerId;
			int32 ConstraintIndex;
		};

		TPBDConstraintGraph();
		TPBDConstraintGraph(const TParticleView<TGeometryParticles<T,d>>& Particles);
		virtual ~TPBDConstraintGraph() {}

		/**
		 * Clear the graph and set up the particle-to-graph-node mapping for the specified particles
		 * Should be called before AddConstraint.
		 */
		void InitializeGraph(const TParticleView<TGeometryParticles<T, d>>& Particles);

		/**
		 * Reserve space in the graph for NumConstraints additional constraints.
		 */
		void ReserveConstraints(const int32 NumConstraints);

		/**
		 * Add a constraint to the graph for each constraint in the container.
		 */
		void AddConstraint(const uint32 InContainerId, const int32 InConstraintIndex, const TVector<TGeometryParticleHandle<T,d>*, 2>& InConstrainedParticles);

		/**
		 * Add particles/constraints to their particle's already-assigned islands (if applicable).
		 */
		 // @todo(ccaulfield): InitializeIslands and ResetIslands are a bit confusing. Try to come up with better names.
		void ResetIslands(const TParticleView<TPBDRigidParticles<T, d>>& PBDRigids);

		/**
		 * Generate the simulation islands of connected particles (AddConstraints must have already been called). There are no constraints connecting particles in different islands.
		 */
		void UpdateIslands(const TParticleView<TPBDRigidParticles<T, d>>& confusing, TPBDRigidsSOAs<T, d>& Particles);

		/**
		 * Put particles in inactive islands to sleep.
		 */
		bool SleepInactive(const int32 Island, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterialAttributes);

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
		 * Get the list of ConstraintData indices associated with the specified island. NOTE: ConstraintDataIndex != ConstraintIndex.
		 * Indices returned are into the ConstraintData array in the ConstraintGraph to get to the Constraint Index and Container Id
		 * @see GetConstraintsData().
		 */
		const TArray<int32>& GetIslandConstraintData(int32 Island) const
		{
			return IslandToConstraints[Island];
		}

		/**
		 * Get the list of Particle indices associated with the specified island.
		 */
		const TArray<TGeometryParticleHandle<T,d>*>& GetIslandParticles(int32 Island) const
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
		void EnableParticle(TGeometryParticleHandle<T,d>* Particle, const TGeometryParticleHandle<T, d>* ParentParticle);

		/**
		 * Disable a particle and remove it from its island.
		 * @note: this does not remove any attached constraints - constraints need to be re-added and islands will need to be rebuilt.
		 */
		void DisableParticle(TGeometryParticleHandle<T, d>* Particle);

		//Remove particle from constraint, maybe rethink some of these names
		void RemoveParticle(TGeometryParticleHandle<T, d>* Particle)
		{
			//we know it just removes, probably rename later
			DisableParticle(Particle);
		}

		/**
		 * Disable a set of particles and remove them from their island.
		 * @note: this does not remove any attached constraints - constraints need to be re-added and islands will need to be rebuilt.
		 */
		void DisableParticles(const TSet<TGeometryParticleHandle<T,d>*>& Particles);

	private:
		struct FGraphNode
		{
			FGraphNode()
				: Particle(nullptr)
				, Island(INDEX_NONE)
			{
			}

			TArray<int32> Edges;
			TGeometryParticleHandle<T,d>* Particle;
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

		void ComputeIslands(TPBDRigidsSOAs<T,d>& Particles);
		void ComputeIsland(const int32 Node, const int32 Island,
			TSet<TGeometryParticleHandle<T, d>*>& DynamicParticlesInIsland, TSet<TGeometryParticleHandle<T, d>*>& StaticParticlesInIsland);
		bool CheckIslands(const TArray<TGeometryParticleHandle<T, d>*>& Particles);

		TArray<FGraphNode> Nodes;
		TArray<FGraphEdge> Edges;
		TArray<FIslandData> IslandToData;
		TMap<TGeometryParticleHandle<T,d>*, int32> ParticleToNodeIndex;

		TArray<TArray<TGeometryParticleHandle<T,d>*>> IslandToParticles;
		TArray<TArray<int32>> IslandToConstraints;
		TArray<int32> IslandToSleepCount;
	};

}
