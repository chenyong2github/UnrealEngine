// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/Evolution/SolverDatas.h"

namespace Chaos
{
	
/** Forward Declaration */
class FPBDIslandManager;

/**
* List of per island datas that will created bu the island manager
*/
class CHAOS_API FPBDIslandSolver : public FPBDIslandSolverData
{
	public:

	/**
	* Init the solver island with an island manager and an index
	* @param IslandManager Island manager that is storing the solver islands
	* @param IslandIndex Index of the solver island within that list
	*/
	FPBDIslandSolver(const FPBDIslandManager* IslandManager, const int32 IslandIndex);

	/**
	* Clear all the particles from the island
	*/
	void ClearParticles();

	/**
	* Add particle to the island
	* @param ParticleHandle ParticleHandle to be added
	*/
	void AddParticle(FGenericParticleHandle ParticleHandle);

	/**
	* Reset the particles list and reserve a number of particles in memory
	* @param NumParticles Number of particles to be reserved
	*/
	void ReserveParticles(const int32 NumParticles);

	/**
	* Remove particle from the island
	* @param ParticleHandle ParticleHandle to be removed
	*/
	void RemoveParticle(FGenericParticleHandle ParticleHandle);
	
	/**
	* Update the particles island index to match the graph index
	*/
	void UpdateParticles();
	
	/**
	* Reset the constraints list and reserve a number of constraints in memory 
	* @param NumConstraints number of constraints to be reserved
	*/
	void ReserveConstraints(const int32 NumConstraints);

	/**
	* Remove all the constraints from the solver island
	*/
	void ClearConstraints();

	/**
	* Add constraint to the island
	* @param ConstraintHandle ConstraintHandle to be added
	*/
	void AddConstraint(FConstraintHandle* ConstraintHandle);

	/**
	* Remove constraint from the island
	* @param ConstraintHandle ConstraintHandle to be removed
	*/
	void RemoveConstraint(FConstraintHandle* ConstraintHandle);

	/**
	* Reset all the island constraint graph index
	*/
	void ResetIndices();

	/**
	* Sort the islands constraints
	*/
	void SortConstraints();

	/**
	 * Set the island group index
	 */
	FORCEINLINE void SetGroupIndex(const int32 GroupIndex) { IslandGroup = GroupIndex; }
	
	/**
	* Get the island group index
	*/
	FORCEINLINE const int32& GetGroupIndex() const { return IslandGroup; }

	/**
	* Return the list of particles within the solver island
	*/
	FORCEINLINE const TArray<FGeometryParticleHandle*>& GetParticles() const { return IslandParticles; }

	/**
	* Return the list of constraints within the island
	*/
	FORCEINLINE const TArray<FConstraintHandleHolder>& GetConstraints() const { return IslandConstraints; }

	/**
	* Get the number of particles within the island
	*/
	FORCEINLINE int32 NumParticles() const {return IslandParticles.Num(); }

	/**
	* Get the number of constraints within the island
	*/
	FORCEINLINE int32 NumConstraints() const {return IslandConstraints.Num(); }
	
	/**
	* Members accessors
	*/
	FORCEINLINE bool IsSleeping() const {return bIsSleeping;}
	FORCEINLINE void SetIsSleeping(const bool bIsSleepingIn ) { bSleepingChanged = (bIsSleeping != bIsSleepingIn); bIsSleeping = bIsSleepingIn; }
	FORCEINLINE bool IsPersistent() const { return bIsPersistent; }
	FORCEINLINE void SetIsPersistent(const bool bIsPersistentIn) { bIsPersistent = bIsPersistentIn; }
	FORCEINLINE bool NeedsResim() const { return bNeedsResim; }
	FORCEINLINE void SetNeedsResim(const bool bNeedsResimIn) { bNeedsResim = bNeedsResimIn; }
	FORCEINLINE int32 GetSleepCounter() const { return SleepCounter; }
	FORCEINLINE void SetSleepCounter(const int32 SleepCounterIn) { SleepCounter = SleepCounterIn; }
	FORCEINLINE bool SleepingChanged() const { return bSleepingChanged; }
	FORCEINLINE void SetIsUsingCache(const bool bIsUsingCacheIn ) { bIsUsingCache = bIsUsingCacheIn; }
	FORCEINLINE bool IsUsingCache() const { return bIsUsingCache; }
	
	// template<typename ConstraintType>
	// void GatherSolverInput(const FReal Dt, const int32 IslandIndex, const int32 ContainerId);
	
	private:

	/** Island manager that is storing the IslandSolver */
	const FPBDIslandManager* IslandManager = nullptr;

	/** Flag to check if an island is awake or sleeping */
	bool bIsSleeping = false;

	/** Flag to check if an island need to be re-simulated or not */
	bool bNeedsResim = false;

	/** Flag to check if an island is persistent over time */
	bool bIsPersistent = true;

	/** Flag to check if the sleeping state has changed or not */
	bool bSleepingChanged = false;

	/** Sleep counter to trigger island sleeping */
	int32 SleepCounter = 0;

	/** List of all the island particles handles */
	TArray<FGeometryParticleHandle*> IslandParticles;

	/** List of all the island constraints handles */
	TArray<FConstraintHandleHolder> IslandConstraints;

	/** Island Group in which the solver belongs */
	int32 IslandGroup = 0;
	
	/** Check if the island is using the cache or not */
	bool bIsUsingCache = false;
	
};
	
// template<typename ConstraintType>
// FORCEINLINE void FPBDIslandSolver::GatherSolverInput(const FReal Dt, const int32 IslandIndex, const int32 ContainerId)
// {
// 	using FConstraintContainerHandle = typename ConstraintType::FConstraintContainerHandle;
// 	if(ConstraintContainers.IsValidIndex(ContainerId))
// 	{
// 		for (FConstraintHandle* ConstraintHandle : IslandConstraints)
// 		{
// 			if (ConstraintHandle->GetContainerId() == ContainerId)
// 			{
// 				FConstraintContainerHandle* Constraint = ConstraintHandle->As<FConstraintContainerHandle>();
//
// 				// Note we are building the SolverBodies as we go, in the order that we visit them. Each constraint
// 				// references two bodies, so we won't strictly be accessing only in cache order, but it's about as good as it can be.
// 				if (Constraint->IsEnabled())
// 				{
// 					// @todo(chaos): we should provide Particle Levels in the island rule as well (see TPBDConstraintColorRule)
// 					Constraint->GatherInput(Dt, GraphIndex, INDEX_NONE, INDEX_NONE, *this);
// 				}
// 			}
// 		}
// 	}
// }
	
}