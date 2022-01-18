// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/SparseArray.h"

#include "Chaos/Real.h"
#include "Chaos/Vector.h"
#include "Chaos/Framework/Handles.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/Island/IslandGraph.h"
#include "Chaos/Island/SolverIsland.h"
#include "Chaos/Island/IslandGroup.h"

namespace Chaos
{

/** Forward declaration */
template<typename T, int D>
class TPBDRigidParticles;
template<class T>
class TChaosPhysicsMaterial;
template <typename T>
class TSerializablePtr;
template<typename T>
class TArrayCollectionArray;
template <typename T>
class TParticleView;
class FPBDRigidsSOAs;
class FConstraintHandle;
class FChaosPhysicsMaterial;

using FPBDRigidParticles = TPBDRigidParticles<FReal, 3>;


/** Island manager responsible to create the list of solver islands that will be persistent over time */
class CHAOS_API FPBDIslandManager
{
public:
	using GraphType = FIslandGraph<FGeometryParticleHandle*, FConstraintHandleHolder, FPBDIslandSolver*>;
	using FGraphNode = GraphType::FGraphNode;
	using FGraphEdge = GraphType::FGraphEdge;
	
	/**
	  * Default island manager constructor
	  */
	FPBDIslandManager();
	
	/**
	* Default island manager destructor
	*/
	~FPBDIslandManager();

	/**
	* Reset nodes and edges graph indices 
	*/
	void ResetIndices();

	/**
	* Remove all the constraints from the graph
	*/
	void RemoveConstraints();

	/**
	  * Default island manager constructor
	  * @param Particles List of particles to be used to fill the graph nodes
	  */
	FPBDIslandManager(const TParticleView<FPBDRigidParticles>& PBDRigids);

	/**
	* Clear the manager and set up the particle-to-graph-node mapping for the specified particles
	* Should be called before AddConstraint.
	* @param Particles List of particles to be used to fill the graph nodes
	*/
	void InitializeGraph(const TParticleView<FPBDRigidParticles>& Particles);

	/**
	  * Reserve space in the graph for NumConstraints additional constraints.
	  * @param NumConstraints Number of constraints to be used to reserved memory space
	  */
	void ReserveConstraints(const int32 NumConstraints);

	/**
	  * Add a constraint to the graph for each constraint in the container.
	  * @param ContainerId Contaner id the constraint is belonging to
	  * @param ConstraintHandle Constraint Handle that will be used for the edge item
	  * @param ConstrainedParticles List of 2 particles handles that are used within the constraint
	  * @return Edge index within the sparse graph edges list
	  */
	int32 AddConstraint(const uint32 ContainerId, FConstraintHandle* ConstraintHandle, const TVector<FGeometryParticleHandle*, 2>& ConstrainedParticles);

	/**
	  * Remove a constraint from the graph
	  * @param ContainerId Container id the constraint is belonging to
	  * @param ConstraintHandle Constraint Handle that will be used for the edge item
	  * @param ConstrainedParticles List of 2 particles handles that are used within the constraint
	  */
	void RemoveConstraint(const uint32 ContainerId, FConstraintHandle* ConstraintHandle);
	
	/**
	  * Preallocate buffers for \p Num particles.
	  * @param NumParticles Number of particles to be used to reserve memory
	  * @return Number of new slots created.
	  */
	int32 ReserveParticles(const int32 NumParticles);

	/**
	  * Add a particle to the Island graph
	  * @param AddedParticle Particle to be added to the graph
	  * @param IslandIndex Potential island index in which the particle will be added
	  * @param bOnlyDynamic Boolean to add only dynamic particle into the islands
	  * @return Sparse graph node index
	  */
	int32 AddParticle(FGeometryParticleHandle* AddedParticle, const int32 IslandIndex = INDEX_NONE, const bool bOnlyDynamic = true);

	/**
	  * Remove a particle from the graph
	  * @param Particle Particle to be removed from the islands
	  */
	void RemoveParticle(FGeometryParticleHandle* Particle);

	/**
	  * Adds \p ChildParticle to the constraint graph.  Copies the sleeping
	  * state and island of \p ParentParticle to \p ChildParticle.  Does nothing
	  * if \p ParentParticle is not supplied.
	  * @param ChildParticle Particle on which the sleeping state and island index will be copied
	  * @param ParentParticle Particle from which the sleeping state and island index will be extracted
	  */
	void EnableParticle(FGeometryParticleHandle* ChildParticle, const FGeometryParticleHandle* ParentParticle);

	/**
	  * Disable a particle and remove it from its island.
	  * @note: this does not remove any attached constraints - constraints need to be re-added and islands will need to be rebuilt.
	  * @param Particle Particle to be disabled
	  */
	void DisableParticle(FGeometryParticleHandle* Particle);

	/**
	  * Remove all the graph edges
	  */
	void ResetIslands(const TParticleView<FPBDRigidParticles>& PBDRigids);

	/**
	  * Generate the simulation islands of connected particles (AddConstraints must have already been called). There are no constraints connecting particles in different islands.
	  * @param PBDRigids Rigid particles that will be used to fill the solver islands
	  * @param Particles SOA used in the evolution
	  */
	void UpdateIslands(const TParticleView<FPBDRigidParticles>& PBDRigids, FPBDRigidsSOAs& Particles, const int32 NumContainers);

	/**
	  * @brief Put all particles and constraints in an island to sleep
	  * @param Particles used in the islands
	  * @param IslandIndex Island index to be woken up
	*/
	void SleepIsland(FPBDRigidsSOAs& Particles, const int32 IslandIndex);

	/**
	  * Wake all particles and constraints in an Island.
	  * @param Particles used in the islands
	  * @param IslandIndex Island index to be woken up
	  */
	void WakeIsland(FPBDRigidsSOAs& Particles, const int32 IslandIndex);

	/**
	  * Put particles in inactive islands to sleep.
	  * @param IslandIndex Island index
	  * @param PerParticleMaterialAttributes Material attributes that has been set on each particles
	  * @param SolverPhysicsMaterials Solver materials that could be used to find the sleeping thresholds
	  * @return return true if the island is going to sleep
	  */
	bool SleepInactive(const int32 IslandIndex, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterialAttributes, const THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials);
	
	/**
	  * Get the island particles stored in the solver island
	  * @param IslandIndex Island index
	  * @return List of particles handles in the island
	  */
	const TArray<FGeometryParticleHandle*>& GetIslandParticles(const int32 IslandIndex) const;

	/**
	  * Get the island constraints stored in the solver island
	  * @param IslandIndex Island index
	  * @return List of constraints handles in the island
	  */
	const TArray<FConstraintHandleHolder>& GetIslandConstraints(const int32 IslandIndex) const;

	/**
	 * When resim is used, tells us whether we need to resolve island
	 * @param IslandIndex Island index
	 * @return If true the island need to be resim
	 */
	bool IslandNeedsResim(const int32 IslandIndex) const;

	/**
	  * The number of islands in the graph.
	  * @return the number of islands
	  */
	FORCEINLINE int32 NumIslands() const
	{
		return IslandIndexing.Num();
	}

	/**
	* The number of groups in the graph.
	* @return the number of groups
	*/
	FORCEINLINE int32 NumGroups() const
	{
		return IslandGroups.Num();
	}
	
	/**
	 * The number of islands in the graph.
	 * @return Get the island graph
	 */
	FORCEINLINE const GraphType* GetIslandGraph() const
	{
		return IslandGraph.Get();
	}
	/**
	* Get the island graph
	* @return Island graph of the manager
	*/
	FORCEINLINE GraphType* GetIslandGraph()
	{
		return IslandGraph.Get();
	}

	/**
	 * Get Max UniqueIdx of the particles.
	 * Used to create arrays large enough to use Particle.UniqueIdx().Idx for indexing.
	 * @return the max index of the particles in the islands
	 */
	inline int32 GetMaxParticleUniqueIdx() const { return MaxParticleIndex; }


	/**
	 * Get the sparse island graph index given a dense index between 0 and NumIslands
	 * @param IslandIndex Island index
	 * @return the max index of the particles in the islands
	 */
	int32 GetGraphIndex(const int32 IslandIndex) const {
		return IslandIndexing.IsValidIndex(IslandIndex) ? IslandIndexing[IslandIndex] : 0;
	}
	
	/** Add a constraint container to all the solver island given a container id
	* @param ContainerId Constraints container id from which the solver constraint containers are being built
	*/
	template<typename ConstraintType>
	void AddConstraintDatas(const int32 ContainerId)
	{
		for(auto& IslandGroup : IslandGroups)
		{
			IslandGroup->AddConstraintDatas<ConstraintType>(ContainerId);
		}
		for(auto& IslandSolver : IslandSolvers)
		{
			IslandSolver->AddConstraintDatas<ConstraintType>(ContainerId);
		}
	}

	// /** Add a constraint container to all the solver island given a container id
	// * @param ContainerId Constraints container id from which the solver constraint containers are being built
	// */
	// template<typename ConstraintType>
	// void GatherSolverInput(const FReal Dt, int32 IslandIndex, const int32 ContainerId)
	// {
	// 	if(IslandIndexing.IsValidIndex(IslandIndex))
	// 	{
	// 		IslandSolvers[IslandIndexing[IslandIndex]]->GatherSolverInput<ConstraintType>(Dt, IslandIndex, ContainerId);
	// 	}
	// }

	/** Accessors of the graph group */
	const FPBDIslandGroup* GetIslandGroup(const int32 GroupIndex) const {return IslandGroups.IsValidIndex(GroupIndex) ? IslandGroups[GroupIndex].Get() : nullptr; }
	FPBDIslandGroup* GetIslandGroup(const int32 GroupIndex) {return IslandGroups.IsValidIndex(GroupIndex) ? IslandGroups[GroupIndex].Get()  : nullptr; }

	/** Accessors of the graph island */
	const FPBDIslandSolver* GetSolverIsland(const int32 IslandIndex) const {return IslandIndexing.IsValidIndex(IslandIndex) ? IslandSolvers[IslandIndexing[IslandIndex]].Get() : nullptr; }
	FPBDIslandSolver* GetSolverIsland(const int32 IslandIndex) {return IslandIndexing.IsValidIndex(IslandIndex) ? IslandSolvers[IslandIndexing[IslandIndex]].Get() : nullptr; }

	/** Accessors of the group islands */
	const TArray<FPBDIslandSolver*>& GetGroupIslands(const int32 GroupIndex) const {return IslandGroups[GroupIndex]->GetIslands(); }
	TArray<FPBDIslandSolver*>& GetGroupIslands(const int32 GroupIndex) {return IslandGroups[GroupIndex]->GetIslands(); }

	/** Accessors of all the graph groups */
	const TArray<TUniquePtr<FPBDIslandGroup>>& GetIslandGroups() const {return IslandGroups; }
	TArray<TUniquePtr<FPBDIslandGroup>>& GetIslandGroups() {return IslandGroups; }

	/** Get the graph nodes */
	const TSparseArray<GraphType::FGraphNode>& GetGraphNodes() const { return IslandGraph->GraphNodes; }
	
	/** Get the graph edges */
	const TSparseArray<GraphType::FGraphEdge>& GetGraphEdges() const { return IslandGraph->GraphEdges; }

	/** Get the particle nodes */
	const TMap<FGeometryParticleHandle*, int32>& GetParticleNodes() const { return IslandGraph->ItemNodes; }
	
	/** Get the constraints edges */
	const TMap<FConstraintHandleHolder, int32>& GetConstraintEdges() const { return IslandGraph->ItemEdges; }
	
protected:

	/**
	* Initialize the groups according to the number of threads
	*/
	void InitializeGroups();

	/**
	 * Build all the island groups
	 */
	void BuildGroups(const int32 NumContainers);

	/**
	* Sync the IslandSolvers with the IslandGraph
	*/
	void SyncIslands(FPBDRigidsSOAs& Particles, const int32 NumContainers);

	/** List of solver islands in the manager */
	TSparseArray<TUniquePtr<FPBDIslandSolver>> IslandSolvers;

	/** Island map to make the redirect an index between 0...NumIslands to the persistent island index  */
	TArray<int32>							IslandIndexing;

	/** Global graph to deal with merging and splitting of islands */
	TUniquePtr<GraphType>					IslandGraph;

	/** Maximum particle index that we have in the graph*/
	int32									MaxParticleIndex = INDEX_NONE;
	
	/** List of island groups in the manager */
	TArray<TUniquePtr<FPBDIslandGroup>> IslandGroups;

	/** Sorted list of islands */
	TArray<int32>							SortedIslands;
};
}