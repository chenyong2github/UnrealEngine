// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Island/IslandGroup.h"
#include "Chaos/Island/SolverIsland.h"

namespace Chaos
{
	
FPBDIslandGroup::FPBDIslandGroup(const int32 GroupIndexIn ) : FPBDIslandSolverData(GroupIndexIn),
	 IslandSolvers(), ParticlesCount(0), ConstraintsCount(0)
{}

void FPBDIslandGroup::ReserveIslands(const int32 NumSolvers)
{
	IslandSolvers.Reserve(NumSolvers);
}

void FPBDIslandGroup::AddIsland(FPBDIslandSolver* IslandSolver)
{
	if (IslandSolver)
	{
		IslandSolvers.Add(IslandSolver);
	}
}
	
void FPBDIslandGroup::ClearIslands()
{
	IslandSolvers.Reset();
}

void FPBDIslandGroup::InitGroup()
{
	IslandSolvers.SetNum(0,false);
	ParticlesCount = 0;
	ConstraintsCount = 0;
}
	

}