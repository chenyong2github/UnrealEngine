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
	typedef typename Base::EMode EMode;

	FXPBDLongRangeConstraints(
		const FPBDParticles& Particles,
		const TMap<int32, TSet<int32>>& PointToNeighbors,
		const int32 MaxNumTetherIslands = 4,
		const FReal InStiffness = (FReal)1.,
		const FReal LimitScale = (FReal)1.,
		const EMode InMode = EMode::Geodesic)
	    : FPBDLongRangeConstraintsBase(Particles, PointToNeighbors, MaxNumTetherIslands, InStiffness, LimitScale, InMode)
	{
		Lambdas.Reserve(Tethers.Num());
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
		static const int32 MinParallelSize = 500;
		TethersView.ParallelFor([this, &Particles, Dt](TArray<FTether>& /*InTethers*/, int32 Index)
			{
				Apply(Particles, Dt, Index);
			}, MinParallelSize);
	}

	void Apply(FPBDParticles& Particles, const FReal Dt, const TArray<int32>& ConstraintIndices) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_LongRange);
		for (const int32 Index : ConstraintIndices)
		{
			Apply(Particles, Dt, Index);
		}
	}

private:
	void Apply(FPBDParticles& Particles, const FReal Dt, int32 Index) const
	{
		const FTether& Tether = Tethers[Index];

		FVec3 Direction;
		FReal Offset;
		Tether.GetDelta(Particles, Direction, Offset);

		FReal& Lambda = Lambdas[Index];
		const FReal Alpha = (FReal)XPBDLongRangeMaxCompliance / (Stiffness * Dt * Dt);

		const FReal DLambda = (Offset - Alpha * Lambda) / ((FReal)1. + Alpha);
		Particles.P(Tether.End) += DLambda * Direction;
		Lambda += DLambda;
	}

private:
	using Base::Tethers;
	using Base::TethersView;
	using Base::Stiffness;

	mutable TArray<FReal> Lambdas;
};
}
