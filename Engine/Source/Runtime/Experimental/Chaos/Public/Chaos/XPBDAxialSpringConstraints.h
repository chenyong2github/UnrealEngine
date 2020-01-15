// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDAxialSpringConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PerParticleRule.h"
#include "ChaosStats.h"

#include <algorithm>

DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Axial Spring Constraint"), STAT_XPBD_AxialSpring, STATGROUP_Chaos);
namespace Chaos
{
// Stiffness is in N/CM^2, so it needs to be adjusted from the PBD stiffness ranging between [0,1]
static const double XPBDAxialSpringMaxCompliance = 1e-7;  // Max stiffness: 1e+11 N/M^2 = 1e+7 N/CM^2 -> Max compliance: 1e-7 CM^2/N

template<class T, int d>
class TXPBDAxialSpringConstraints : public TParticleRule<T, d>, public TPBDAxialSpringConstraintsBase<T, d>
{
	typedef TPBDAxialSpringConstraintsBase<T, d> Base;
	using Base::MBarys;
	using Base::MConstraints;
	using Base::MDists;
	using Base::MStiffness;

public:
	TXPBDAxialSpringConstraints(const TDynamicParticles<T, d>& InParticles, TArray<TVector<int32, 3>>&& Constraints, const T Stiffness = (T)1.)
	    : TPBDAxialSpringConstraintsBase<T, d>(InParticles, MoveTemp(Constraints), Stiffness)
	{ MLambdas.Init(0.f, MConstraints.Num()); }

	virtual ~TXPBDAxialSpringConstraints() {}

	void Init() const { for (T& Lambda : MLambdas) { Lambda = (T)0.; } }

	virtual void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const override //-V762
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_AxialSpring);
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			const TVector<int32, 3>& constraint = MConstraints[i];
			const int32 i1 = constraint[0];
			const int32 i2 = constraint[1];
			const int32 i3 = constraint[2];
			const TVector<T, d> Delta = GetDelta(InParticles, Dt, i);
			const T Multiplier = (T)2. / (FMath::Max(MBarys[i], (T)1. - MBarys[i]) + (T)1.);
			if (InParticles.InvM(i1) > 0)
			{
				InParticles.P(i1) -= Multiplier * InParticles.InvM(i1) * Delta;
			}
			if (InParticles.InvM(i2))
			{
				InParticles.P(i2) += Multiplier * InParticles.InvM(i2) * MBarys[i] * Delta;
			}
			if (InParticles.InvM(i3))
			{
				InParticles.P(i3) += Multiplier * InParticles.InvM(i3) * (1 - MBarys[i]) * Delta;
			}
		}
	}

private:
	inline TVector<T, d> GetDelta(const TPBDParticles<T, d>& InParticles, const T Dt, const int32 InConstraintIndex) const
	{
		const TVector<int32, 3>& Constraint = MConstraints[InConstraintIndex];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];

		const T Bary = MBarys[InConstraintIndex];
		const T PInvMass = InParticles.InvM(i3) * ((T)1. - Bary) + InParticles.InvM(i2) * Bary;
		if (InParticles.InvM(i1) == (T)0. && PInvMass == (T)0.)
		{
			return TVector<T, d>((T)0.);
		}
		const T CombinedInvMass = PInvMass + InParticles.InvM(i1);
		ensure(CombinedInvMass > (T)SMALL_NUMBER);

		const TVector<T, d>& P1 = InParticles.P(i1);
		const TVector<T, d>& P2 = InParticles.P(i2);
		const TVector<T, d>& P3 = InParticles.P(i3);
		const TVector<T, d> P = (P2 - P3) * Bary + P3;

		const TVector<T, d> Difference = P1 - P;
		const T Distance = Difference.Size();
		if (UNLIKELY(Distance <= SMALL_NUMBER))
		{
			return TVector<T, d>((T)0.);
		}
		const TVector<T, d> Direction = Difference / Distance;
		const T Offset = (Distance - MDists[InConstraintIndex]);

		T& Lambda = MLambdas[InConstraintIndex];
		const T Alpha = (T)XPBDAxialSpringMaxCompliance / (MStiffness * Dt * Dt);

		const T DLambda = (Offset - Alpha * Lambda) / (CombinedInvMass + Alpha);
		const TVector<T, d> Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	}

private:
	mutable TArray<T> MLambdas;
};
}
