// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Island/SolverIsland.h"
#include "Chaos/PBDConstraintRule.h"

namespace Chaos
{
	
FPBDIslandSolver::FPBDIslandSolver(const FPBDIslandManager* IslandManagerIn, const int32 IslandIndexIn) : FPBDIslandSolverData(IslandIndexIn),
	IslandManager(IslandManagerIn), bIsSleeping(false), bNeedsResim(false), bIsPersistent(true), SleepCounter(0),
	IslandParticles(), IslandConstraints()
{}

void FPBDIslandSolver::UpdateParticles()
{
	for (FGenericParticleHandle ParticleHandle : IslandParticles)
	{
		if (ParticleHandle.IsValid() && ParticleHandle->IsDynamic())
		{
			ParticleHandle->SetIsland(IslandIndex);
		}
	}
}
	
void FPBDIslandSolver::ClearParticles()
{
	IslandParticles.Reset();
}

void FPBDIslandSolver::AddParticle(FGenericParticleHandle ParticleHandle)
{
	if (ParticleHandle.IsValid())
	{
		if (ParticleHandle->IsDynamic())
		{
			ParticleHandle->SetIsland(IslandIndex);
		}
		IslandParticles.Add(ParticleHandle->Handle());
	}
}

void FPBDIslandSolver::RemoveParticle(FGenericParticleHandle ParticleHandle)
{
	if (ParticleHandle.IsValid())
	{
		if (ParticleHandle->IsDynamic())
		{
			ParticleHandle->SetIsland(INDEX_NONE);
		}
		IslandParticles.Remove(ParticleHandle->Handle());
	}
}

void FPBDIslandSolver::ReserveParticles(const int32 NumParticles)
{
	ClearParticles();
	IslandParticles.Reserve(NumParticles);
}

void FPBDIslandSolver::AddConstraint(FConstraintHandle* ConstraintHandle)
{
	if (ConstraintHandle)
	{
		IslandConstraints.Add(ConstraintHandle);
	}
}

void FPBDIslandSolver::RemoveConstraint(FConstraintHandle* ConstraintHandle)
{
	if (ConstraintHandle)
	{
		IslandConstraints.Remove(ConstraintHandle);
	}
}

void FPBDIslandSolver::ReserveConstraints(const int32 NumConstraints)
{
	ClearConstraints();
	IslandConstraints.Reserve(NumConstraints);
}

void FPBDIslandSolver::ClearConstraints()
{
	IslandConstraints.Reset();
}

}