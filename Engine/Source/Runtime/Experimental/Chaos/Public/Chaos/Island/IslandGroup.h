// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Chaos/Evolution/SolverDatas.h"

namespace Chaos
{
	
/** Forward Declaration */
class FPBDIslandSolver;

/**
* Group of islands that will be used by evolution to run the solvers steps in parallel
*/
class CHAOS_API FPBDIslandGroup : public FPBDIslandSolverData
{
public:

	/**
	* Init the island group 
	*/
	FPBDIslandGroup(const int32 GroupIndexIn);

	/**
	* Init group members to their default values
	*/
	void InitGroup();
	
	/**
	* Reset the islands list and reserve a number of islands in memory 
	* @param NumIslands number of islands to be reserved
	*/
	void ReserveIslands(const int32 NumIslands);

	/**
	* Add island to the group
	* @param IslandSolver Island Solver top be added
	*/
	void AddIsland(FPBDIslandSolver* IslandSolver);

	/**
	* Remove all islands from the group
	*/
	void ClearIslands();

	/**
	* Check if the group is valid and contains islands
	*/
	FORCEINLINE bool IsValid() const { return (IslandSolvers.Num() > 0); }

	/**
	* Get the number of islands within the group
	*/
	FORCEINLINE int32 NumIslands() const { return IslandSolvers.Num(); }

	/**
	* Return the islands within the group
	*/
	FORCEINLINE const TArray<FPBDIslandSolver*>& GetIslands() const { return IslandSolvers; }

	/**
	* Return the islands within the group
	*/
	FORCEINLINE TArray<FPBDIslandSolver*>& GetIslands() { return IslandSolvers; }

	/**
	* Accessors for the number of particles
	*/
	FORCEINLINE const int32& GetNumParticles() const  { return ParticlesCount; }
	FORCEINLINE int32& NumParticles() {return ParticlesCount; }

	/**
	* Accessors for the number of constraints
	*/
	FORCEINLINE const int32& GetNumConstraints() const  { return ConstraintsCount; }
	FORCEINLINE int32& NumConstraints() {return ConstraintsCount; }
	
private:

	/** List of all the island constraints handles */
	TArray<FPBDIslandSolver*> IslandSolvers;

	/** Number of particles in this group */
	int32 ParticlesCount = 0;

	/** Number of constraints in this group */
	int32 ConstraintsCount = 0;
};
	
}