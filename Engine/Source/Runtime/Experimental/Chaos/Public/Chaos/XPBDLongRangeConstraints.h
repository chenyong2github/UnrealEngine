// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDLongRangeConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Long Range Constraint"), STAT_XPBD_LongRange, STATGROUP_Chaos);

namespace Chaos
{
// Stiffness is in N/CM^2, so it needs to be adjusted from the PBD stiffness ranging between [0,1]
static const double XPBDLongRangeMaxCompliance = 1.e-3;

class FXPBDLongRangeConstraints final : public FPBDLongRangeConstraintsBase
{
public:
	typedef FPBDLongRangeConstraintsBase Base;
	typedef typename Base::FTether FTether;

	FXPBDLongRangeConstraints(
		const FPBDParticles& Particles,
		const int32 InParticleOffset,
		const int32 InParticleCount,
		const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>>& InTethers,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const TConstArrayView<FRealSingle>& ScaleMultipliers,
		const FVec2& InStiffness = FVec2::UnitVector,
		const FVec2& InScale = FVec2::UnitVector)
	    : FPBDLongRangeConstraintsBase(Particles, InParticleOffset, InParticleCount, InTethers, StiffnessMultipliers, ScaleMultipliers, InStiffness, InScale)
	{
		int32 NumTethers = 0;
		for (const TConstArrayView<FTether>& TetherBatch : Tethers)
		{
			NumTethers += TetherBatch.Num();
		}
		Lambdas.Reserve(NumTethers);
	}

	virtual ~FXPBDLongRangeConstraints() {}

	void Init() const
	{
		Lambdas.Reset();
		Lambdas.AddZeroed(Tethers.Num());
	}

	void Apply(FPBDParticles& Particles, const FReal Dt) const 
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_LongRange);
		// Run particles in parallel, and ranges in sequence to avoid a race condition when updating the same particle from different tethers
		const int32 MinParallelSize = GetMinParallelBatchSize();

		if (Stiffness.HasWeightMap())
		{
			if (HasScaleWeightMap())
			{
				int32 ConstraintOffset = 0;
				for (const TConstArrayView<FTether>& TetherBatch : Tethers)
				{
					PhysicsParallelFor(TetherBatch.Num(), [this, &Particles, Dt, &TetherBatch, ConstraintOffset](int32 Index)
						{
							const FTether& Tether = TetherBatch[Index];
							const int32 LocalParticleIndex = GetEndIndex(Tether);
							const FReal Scale = ScaleTable[ScaleIndices[LocalParticleIndex]];
							const FReal ExpStiffnessValue = Stiffness[LocalParticleIndex];
							Apply(Particles, Dt, Tether, ConstraintOffset + Index, ExpStiffnessValue, Scale);
						}, TetherBatch.Num() < MinParallelSize);
					ConstraintOffset += TetherBatch.Num();
				}
			}
			else
			{
				const FReal ScaleValue = ScaleTable[0];
				int32 ConstraintOffset = 0;
				for (const TConstArrayView<FTether>& TetherBatch : Tethers)
				{
					PhysicsParallelFor(TetherBatch.Num(), [this, &Particles, Dt, &TetherBatch, ScaleValue, ConstraintOffset](int32 Index)
						{
							const FTether& Tether = TetherBatch[Index];
							const int32 LocalParticleIndex = GetEndIndex(Tether);
							const FReal ExpStiffnessValue = Stiffness[LocalParticleIndex];
							Apply(Particles, Dt, Tether, ConstraintOffset + Index, ExpStiffnessValue, ScaleValue);
						}, TetherBatch.Num() < MinParallelSize);
					ConstraintOffset += TetherBatch.Num();
				}
			}
		}
		else
		{
			const FReal ExpStiffnessValue = (FReal)Stiffness;

			if (HasScaleWeightMap())
			{
				int32 ConstraintOffset = 0;
				for (const TConstArrayView<FTether>& TetherBatch : Tethers)
				{
					PhysicsParallelFor(TetherBatch.Num(), [this, &Particles, Dt, &TetherBatch, ExpStiffnessValue, ConstraintOffset](int32 Index)
						{
							const FTether& Tether = TetherBatch[Index];
							const int32 LocalParticleIndex = GetEndIndex(Tether);
							const FReal Scale = ScaleTable[ScaleIndices[LocalParticleIndex]];
							Apply(Particles, Dt, Tether, ConstraintOffset + Index, ExpStiffnessValue, Scale);
						}, TetherBatch.Num() < MinParallelSize);
					ConstraintOffset += TetherBatch.Num();
				}
			}
			else
			{
				const FReal ScaleValue = ScaleTable[0];
				int32 ConstraintOffset = 0;
				for (const TConstArrayView<FTether>& TetherBatch : Tethers)
				{
					PhysicsParallelFor(TetherBatch.Num(), [this, &Particles, Dt, &TetherBatch, ExpStiffnessValue, ScaleValue, ConstraintOffset](int32 Index)
						{
							const FTether& Tether = TetherBatch[Index];
							Apply(Particles, Dt, Tether, ConstraintOffset + Index, ExpStiffnessValue, ScaleValue);
						}, TetherBatch.Num() < MinParallelSize);
					ConstraintOffset += TetherBatch.Num();
				}
			}
		}
	}

private:
	void Apply(FPBDParticles& Particles, const FReal Dt, const FTether& Tether, int32 ConstraintIndex, const FReal InStiffness, const FReal InScale) const
	{
		FVec3 Direction;
		FReal Offset;
		GetDelta(Particles, Tether, InScale, Direction, Offset);

		FReal& Lambda = Lambdas[ConstraintIndex];
		const FReal Alpha = (FReal)XPBDLongRangeMaxCompliance / (InStiffness * Dt * Dt);

		const FReal DLambda = (Offset - Alpha * Lambda) / ((FReal)1. + Alpha);
		Particles.P(GetEndParticle(Tether)) += DLambda * Direction;
		Lambda += DLambda;
	}

private:
	using Base::Tethers;
	using Base::Stiffness;
	using Base::ParticleOffset;
	using Base::ScaleTable;
	using Base::ScaleIndices;

	mutable TArray<FReal> Lambdas;
};
}
