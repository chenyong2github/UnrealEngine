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

template<class T, int d>
class TXPBDLongRangeConstraints : public TPBDLongRangeConstraintsBase<T, d>, public FPBDConstraintContainer
{
	typedef TPBDLongRangeConstraintsBase<T, d> Base;
	using Base::MConstraints;
	using Base::MDists;
	using Base::MStiffness;

public:
	TXPBDLongRangeConstraints(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments = 1, const T Stiffness = (T)1)
	    : TPBDLongRangeConstraintsBase<T, d>(InParticles, PointToNeighbors, NumberOfAttachments, Stiffness)
	{ MLambdas.Init(0.f, MConstraints.Num()); }

	virtual ~TXPBDLongRangeConstraints() {}

	void Init() const { for (T& Lambda : MLambdas) { Lambda = (T)0.; } }

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, int32 Index) const
	{
		const auto& Constraint = MConstraints[Index];
		int32 i2 = Constraint[Constraint.Num() - 1];
		check(InParticles.InvM(i2) > 0);
		InParticles.P(i2) += GetDelta(InParticles, Dt, Index);
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const 
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_LongRange);
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_LongRange);
		for (int32 i : InConstraintIndices)
		{
			Apply(InParticles, Dt, i);
		}
	}

private:
	inline TVector<T, d> GetDelta(const TPBDParticles<T, d>& InParticles, const T Dt, const int32 InConstraintIndices) const
	{
		const TArray<uint32>& Constraint = MConstraints[InConstraintIndices];
		check(Constraint.Num() > 1);
		const uint32 i1 = Constraint[0];
		const uint32 i2 = Constraint[Constraint.Num() - 1];
		const uint32 i2m1 = Constraint[Constraint.Num() - 2];
		check(InParticles.InvM(i1) == 0);
		check(InParticles.InvM(i2) > 0);
		const T Distance = Base::ComputeGeodesicDistance(InParticles, Constraint); // This function is used for either Euclidean or Geodesic distances
		if (Distance < MDists[InConstraintIndices]) { return TVector<T, d>(0); }
		const T Offset = Distance - MDists[InConstraintIndices];

		TVector<T, d> Direction = InParticles.P(i2m1) - InParticles.P(i2);
		Direction.SafeNormalize();

		T& Lambda = MLambdas[InConstraintIndices];
		const T Alpha = (T)XPBDLongRangeMaxCompliance / (MStiffness * Dt * Dt);

		const T DLambda = (Offset - Alpha * Lambda) / ((T)1. + Alpha);
		const TVector<T, d> Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	};

private:
	mutable TArray<T> MLambdas;
};
}
