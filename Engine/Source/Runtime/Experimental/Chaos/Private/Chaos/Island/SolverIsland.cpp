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
			ParticleHandle->SetIslandIndex(IslandIndex);
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
			ParticleHandle->SetIslandIndex(IslandIndex);
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
			ParticleHandle->SetIslandIndex(INDEX_NONE);
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
		const int32 ContainerId = ConstraintHandle->GetContainerId();
		if (ConstraintCounts.IsValidIndex(ContainerId))
		{
			IslandConstraints.Add(ConstraintHandle);

			ConstraintCounts[ContainerId]++;
		}
	}
}

void FPBDIslandSolver::RemoveConstraint(FConstraintHandle* ConstraintHandle)
{
	// @todo(chaos): store the Island Constraint Index as a cookie on the constraint
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

// we should remove the one in constraint allocator once this one will be used
inline bool ConstraintSortPredicate(const FConstraintHandle& L, const FConstraintHandle& R)
{
	const FPBDCollisionConstraint* CollisionConstraintL = L.As<FPBDCollisionConstraint>();
	const FPBDCollisionConstraint* CollisionConstraintR = R.As<FPBDCollisionConstraint>();

	if(CollisionConstraintL && CollisionConstraintR)
	{
		//sort constraints by the smallest particle idx in them first
		//if the smallest particle idx is the same for both, use the other idx

		if (CollisionConstraintL->GetCCDType() != CollisionConstraintR->GetCCDType())
		{
			return CollisionConstraintL->GetCCDType() < CollisionConstraintR->GetCCDType();
		}

		const FParticleID ParticleIdxsL[] = { CollisionConstraintL->GetParticle0()->ParticleID(), CollisionConstraintL->GetParticle1()->ParticleID() };
		const FParticleID ParticleIdxsR[] = { CollisionConstraintR->GetParticle0()->ParticleID(), CollisionConstraintL->GetParticle1()->ParticleID() };

		const int32 MinIdxL = ParticleIdxsL[0] < ParticleIdxsL[1] ? 0 : 1;
		const int32 MinIdxR = ParticleIdxsR[0] < ParticleIdxsR[1] ? 0 : 1;

		if(ParticleIdxsL[MinIdxL] < ParticleIdxsR[MinIdxR])
		{
			return true;
		} 
		else if(ParticleIdxsL[MinIdxL] == ParticleIdxsR[MinIdxR])
		{
			return ParticleIdxsL[!MinIdxL] < ParticleIdxsR[!MinIdxR];
		}

		return false;
	}
	return false;
}

inline bool ConstraintHolderSortPredicate(const FConstraintHandleHolder& L, const FConstraintHandleHolder& R)
{
	return ConstraintSortPredicate(*L.Get(), *R.Get());
}


void FPBDIslandSolver::SortConstraints()
{
	if(!IsSleeping())
	{
		IslandConstraints.Sort(ConstraintHolderSortPredicate);
	}
}

}