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

class FXPBDAxialSpringConstraints : public FParticleRule, public FPBDAxialSpringConstraintsBase
{
	typedef FPBDAxialSpringConstraintsBase Base;
	using Base::MBarys;
	using Base::MConstraints;
	using Base::MDists;
	using Base::MStiffness;

public:
	FXPBDAxialSpringConstraints(const FDynamicParticles& InParticles, TArray<TVector<int32, 3>>&& Constraints, const FReal Stiffness = (FReal)1.)
	    : FPBDAxialSpringConstraintsBase(InParticles, MoveTemp(Constraints), Stiffness)
	{ MLambdas.Init(0.f, MConstraints.Num()); }

	virtual ~FXPBDAxialSpringConstraints() {}

	void Init() const { for (FReal& Lambda : MLambdas) { Lambda = (FReal)0.; } }

	virtual void Apply(FPBDParticles& InParticles, const FReal Dt) const override //-V762
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_AxialSpring);
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			const TVector<int32, 3>& constraint = MConstraints[i];
			const int32 i1 = constraint[0];
			const int32 i2 = constraint[1];
			const int32 i3 = constraint[2];
			const FVec3 Delta = GetDelta(InParticles, Dt, i);
			const FReal Multiplier = (FReal)2. / (FMath::Max(MBarys[i], (FReal)1. - MBarys[i]) + (FReal)1.);
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
	inline FVec3 GetDelta(const FPBDParticles& InParticles, const FReal Dt, const int32 InConstraintIndex) const
	{
		const TVector<int32, 3>& Constraint = MConstraints[InConstraintIndex];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];

		const FReal Bary = MBarys[InConstraintIndex];
		const FReal PInvMass = InParticles.InvM(i3) * ((FReal)1. - Bary) + InParticles.InvM(i2) * Bary;
		if (InParticles.InvM(i1) == (FReal)0. && PInvMass == (FReal)0.)
		{
			return FVec3((FReal)0.);
		}
		const FReal CombinedInvMass = PInvMass + InParticles.InvM(i1);
		ensure(CombinedInvMass > (FReal)SMALL_NUMBER);

		const FVec3& P1 = InParticles.P(i1);
		const FVec3& P2 = InParticles.P(i2);
		const FVec3& P3 = InParticles.P(i3);
		const FVec3 P = (P2 - P3) * Bary + P3;

		const FVec3 Difference = P1 - P;
		const FReal Distance = Difference.Size();
		if (UNLIKELY(Distance <= SMALL_NUMBER))
		{
			return FVec3((FReal)0.);
		}
		const FVec3 Direction = Difference / Distance;
		const FReal Offset = (Distance - MDists[InConstraintIndex]);

		FReal& Lambda = MLambdas[InConstraintIndex];
		const FReal Alpha = (FReal)XPBDAxialSpringMaxCompliance / (MStiffness * Dt * Dt);

		const FReal DLambda = (Offset - Alpha * Lambda) / (CombinedInvMass + Alpha);
		const FVec3 Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	}

private:
	mutable TArray<FReal> MLambdas;
};

template<class T, int d>
using TXPBDAxialSpringConstraints UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FXPBDAxialSpringConstraints instead") = FXPBDAxialSpringConstraints;
}
