// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDSpringConstraintsBase.h"
#include "Chaos/PBDConstraintContainer.h"
#include "ChaosStats.h"

#include "Templates/EnableIf.h"

DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Spring Constraint"), STAT_XPBD_Spring, STATGROUP_Chaos);

namespace Chaos
{
// Stiffness is in N/CM^2, so it needs to be adjusted from the PBD stiffness ranging between [0,1]
static const double XPBDSpringMaxCompliance = 1e-7;  // Max stiffness: 1e+11 N/M^2 = 1e+7 N/CM^2 -> Max compliance: 1e-7 CM^2/N

template<class T, int32 d>
class TXPBDSpringConstraints : public TPBDSpringConstraintsBase<T, d>, public FPBDConstraintContainer
{
	typedef TPBDSpringConstraintsBase<T, d> Base;
	using Base::MConstraints;
	using Base::MDists;
	using Base::MStiffness;

public:
	TXPBDSpringConstraints(const T Stiffness = (T)1)
	    : TPBDSpringConstraintsBase<T, d>(Stiffness)
	{}

	TXPBDSpringConstraints(const TDynamicParticles<T, d>& InParticles, TArray<TVector<int32, 2>>&& Constraints, const T Stiffness = (T)1., bool bStripKinematicConstraints = false)
		: TPBDSpringConstraintsBase<T, d>(InParticles, MoveTemp(Constraints), Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init(0.f, MConstraints.Num());
	}

	TXPBDSpringConstraints(const TRigidParticles<T, d>& InParticles, TArray<TVector<int32, 2>>&& Constraints, const T Stiffness = (T)1., bool bStripKinematicConstraints = false)
		: TPBDSpringConstraintsBase<T, d>(InParticles, MoveTemp(Constraints), Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init(0.f, MConstraints.Num());
	}

	TXPBDSpringConstraints(const TDynamicParticles<T, d>& InParticles, const TArray<TVector<int32, 3>>& Constraints, const T Stiffness = (T)1., bool bStripKinematicConstraints = false)
		: TPBDSpringConstraintsBase<T, d>(InParticles, Constraints, Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init(0.f, MConstraints.Num());
	}

	TXPBDSpringConstraints(const TDynamicParticles<T, d>& InParticles, const TArray<TVector<int32, 4>>& Constraints, const T Stiffness = (T)1., bool bStripKinematicConstraints = false)
		: TPBDSpringConstraintsBase<T, d>(InParticles, Constraints, Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init(0.f, MConstraints.Num());
	}

	virtual ~TXPBDSpringConstraints() {}

	const TArray<TVector<int32, 2>>& GetConstraints() const { return MConstraints; }
	TArray<TVector<int32, 2>>& GetConstraints() { return MConstraints; }
	TArray<TVector<int32, 2>>& Constraints() { return MConstraints; }

	void Init() const { for (T& Lambda : MLambdas) { Lambda = (T)0.; } }

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 InConstraintIndex) const
	{
		const int32 i = InConstraintIndex;
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];
			const TVector<T, d> Delta = GetDelta(InParticles, Dt, i);
			if (InParticles.InvM(i1) > 0)
			{
				InParticles.P(i1) -= InParticles.InvM(i1) * Delta;
			}
			if (InParticles.InvM(i2) > 0)
			{
				InParticles.P(i2) += InParticles.InvM(i2) * Delta;
			}
		}
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_Spring);
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_Spring);
		for (int32 i : InConstraintIndices)
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];
			check(InParticles.Island(i1) == InParticles.Island(i2) || InParticles.Island(i1) == INDEX_NONE || InParticles.Island(i2) == INDEX_NONE);
			Apply(InParticles, Dt, i);
		}
	}

private:
	inline TVector<T, d> GetDelta(const TPBDParticles<T, d>& InParticles, const T Dt, const int32 InConstraintIndex) const
	{
		const TVector<int32, 2>& Constraint = MConstraints[InConstraintIndex];

		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];

		if (InParticles.InvM(i2) == 0 && InParticles.InvM(i1) == 0)
			return TVector<T, d>(0);
		const T CombinedInvMass = InParticles.InvM(i2) + InParticles.InvM(i1);

		const TVector<T, d>& P1 = InParticles.P(i1);
		const TVector<T, d>& P2 = InParticles.P(i2);
		TVector<T, d> Direction = P1 - P2;
		const T Distance = Direction.SafeNormalize();
		const T Offset = Distance - MDists[InConstraintIndex];

		T& Lambda = MLambdas[InConstraintIndex];
		const T Alpha = (T)XPBDSpringMaxCompliance / (MStiffness * Dt * Dt);

		const T DLambda = (Offset - Alpha * Lambda) / (CombinedInvMass + Alpha);
		const TVector<T, d> Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	}

private:
	mutable TArray<T> MLambdas;
};
}
