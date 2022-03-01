// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/Evolution/SolverDatas.h"

namespace Chaos
{
	TVector<FGeometryParticleHandle*, 2> FPBDPositionConstraintHandle::GetConstrainedParticles() const 
	{ 
		return ConcreteContainer()->GetConstrainedParticles(ConstraintIndex);
	}

	void FPBDPositionConstraintHandle::PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData)
	{
		ConcreteContainer()->PreGatherInput(Dt, ConstraintIndex, SolverData);
	}

	void FPBDPositionConstraintHandle::GatherInput(const FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
	{
		ConcreteContainer()->GatherInput(Dt, ConstraintIndex, Particle0Level, Particle1Level, SolverData);
	}
	
	void FPBDPositionConstraints::SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData)
	{
		SolverData.GetConstraintIndices(ContainerId).Reset(NumIslandConstraints);
	}

	void FPBDPositionConstraints::PreGatherInput(const FReal Dt, const int32 ConstraintIndex, FPBDIslandSolverData& SolverData)
	{
		SolverData.GetConstraintIndices(ContainerId).Add(ConstraintIndex);

		ConstraintSolverBodies[ConstraintIndex] = SolverData.GetBodyContainer().FindOrAdd(ConstrainedParticles[ConstraintIndex]);
	}

	void FPBDPositionConstraints::GatherInput(const FReal Dt, const int32 ConstraintIndex, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
	{
	}
	
	void FPBDPositionConstraints::ScatterOutput(FReal Dt, FPBDIslandSolverData& SolverData)
	{
		for (int32 ConstraintIndex : SolverData.GetConstraintIndices(ContainerId))
		{
			ConstraintSolverBodies[ConstraintIndex] = nullptr;
		}
	}

	bool FPBDPositionConstraints::ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData)
	{
		for (int32 ConstraintIndex : SolverData.GetConstraintIndices(ContainerId))
		{
			ApplySingle(Dt, ConstraintIndex);
		}

		// @todo(chaos): early iteration termination in FPBDPositionConstraints
		return true;
	}
}
