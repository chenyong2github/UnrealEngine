// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDLongRangeConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDConstraintContainer.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Long Range Constraint"), STAT_XPBD_LongRange, STATGROUP_Chaos);

namespace Chaos
{
// Stiffness is in N/CM^2, so it needs to be adjusted from the PBD stiffness ranging between [0,1]
static const double XPBDLongRangeMaxCompliance = 1.e-3;

class FXPBDLongRangeConstraints : public FPBDLongRangeConstraintsBase, public FPBDConstraintContainer
{
	typedef FPBDLongRangeConstraintsBase Base;
	using Base::MEuclideanConstraints;
	using Base::MDists;
	using Base::MStiffness;

public:
	FXPBDLongRangeConstraints(const FDynamicParticles& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments = 1, const FReal Stiffness = (FReal)1.)
	    : FPBDLongRangeConstraintsBase(InParticles, PointToNeighbors, NumberOfAttachments, Stiffness)
	{ MLambdas.Init(0.f, MEuclideanConstraints.Num()); }

	virtual ~FXPBDLongRangeConstraints() {}

	void Init() const { for (FReal& Lambda : MLambdas) { Lambda = (FReal)0.; } }

	void Apply(FPBDParticles& InParticles, const FReal Dt, int32 Index) const
	{
		const auto& Constraint = MEuclideanConstraints[Index];
		int32 i2 = Constraint[Constraint.Num() - 1];
		check(InParticles.InvM(i2) > 0);
		InParticles.P(i2) += GetDelta(InParticles, Dt, Index);
	}

	void Apply(FPBDParticles& InParticles, const FReal Dt) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_LongRange);
		for (int32 i = 0; i < MEuclideanConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(FPBDParticles& InParticles, const FReal Dt, const TArray<int32>& InConstraintIndices) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_LongRange);
		for (int32 i : InConstraintIndices)
		{
			Apply(InParticles, Dt, i);
		}
	}

private:
	inline FVec3 GetDelta(const FPBDParticles& InParticles, const FReal Dt, const int32 InConstraintIndices) const
	{
		const TVector<uint32, 2>& Constraint = MEuclideanConstraints[InConstraintIndices];
		check(Constraint.Num() > 1);
		const uint32 i1 = Constraint[0];
		const uint32 i2 = Constraint[1];
		check(InParticles.InvM(i1) == 0);
		check(InParticles.InvM(i2) > 0);
		const FReal Distance = Base::ComputeGeodesicDistance(InParticles, Constraint); // This function is used for either Euclidean or Geodesic distances
		if (Distance < MDists[InConstraintIndices]) { return FVec3((FReal)0.); }
		const FReal Offset = Distance - MDists[InConstraintIndices];

		FVec3 Direction = InParticles.P(i1) - InParticles.P(i2);
		Direction.SafeNormalize();

		FReal& Lambda = MLambdas[InConstraintIndices];
		const FReal Alpha = (FReal)XPBDLongRangeMaxCompliance / (MStiffness * Dt * Dt);

		const FReal DLambda = (Offset - Alpha * Lambda) / ((FReal)1. + Alpha);
		const FVec3 Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	};

private:
	mutable TArray<FReal> MLambdas;
};
}
