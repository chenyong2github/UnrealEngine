// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDAxialSpringConstraintsBase.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Axial Spring Constraint"), STAT_XPBD_AxialSpring, STATGROUP_Chaos);

namespace Chaos
{

// Stiffness is in N/CM^2, so it needs to be adjusted from the PBD stiffness ranging between [0,1]
static const double XPBDAxialSpringMaxCompliance = 1e-7;  // Max stiffness: 1e+11 N/M^2 = 1e+7 N/CM^2 -> Max compliance: 1e-7 CM^2/N

class FXPBDAxialSpringConstraints : public FPBDAxialSpringConstraintsBase
{
	typedef FPBDAxialSpringConstraintsBase Base;
	using Base::Barys;
	using Base::Constraints;
	using Base::Dists;
	using Base::Stiffness;

public:
	FXPBDAxialSpringConstraints(
		const FPBDParticles& Particles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const TArray<TVec3<int32>>& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const FVec2& InStiffness,
		bool bTrimKinematicConstraints)
		: Base(Particles, ParticleOffset, ParticleCount, InConstraints, StiffnessMultipliers, InStiffness, bTrimKinematicConstraints)
	{
		Lambdas.Init(0.f, Constraints.Num());
	}

	virtual ~FXPBDAxialSpringConstraints() {}

	void Init() const { for (FReal& Lambda : Lambdas) { Lambda = (FReal)0.; } }

	void Apply(FPBDParticles& Particles, const FReal Dt) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_AxialSpring);
		if (!Stiffness.HasWeightMap())
		{
			const FReal ExpStiffnessValue = (FReal)Stiffness;
			for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
			{
				const TVector<int32, 3>& constraint = Constraints[ConstraintIndex];
				const int32 i1 = constraint[0];
				const int32 i2 = constraint[1];
				const int32 i3 = constraint[2];
				const FVec3 Delta = GetDelta(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
				const FReal Multiplier = (FReal)2. / (FMath::Max(Barys[ConstraintIndex], (FReal)1. - Barys[ConstraintIndex]) + (FReal)1.);
				if (Particles.InvM(i1) > 0)
				{
					Particles.P(i1) -= Multiplier * Particles.InvM(i1) * Delta;
				}
				if (Particles.InvM(i2))
				{
					Particles.P(i2) += Multiplier * Particles.InvM(i2) * Barys[ConstraintIndex] * Delta;
				}
				if (Particles.InvM(i3))
				{
					Particles.P(i3) += Multiplier * Particles.InvM(i3) * ((FReal)1. - Barys[ConstraintIndex]) * Delta;
				}
			}
		}
		else
		{
			for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
			{
				const FReal ExpStiffnessValue = Stiffness[ConstraintIndex];
				const TVector<int32, 3>& constraint = Constraints[ConstraintIndex];
				const int32 i1 = constraint[0];
				const int32 i2 = constraint[1];
				const int32 i3 = constraint[2];
				const FVec3 Delta = GetDelta(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
				const FReal Multiplier = (FReal)2. / (FMath::Max(Barys[ConstraintIndex], (FReal)1. - Barys[ConstraintIndex]) + (FReal)1.);
				if (Particles.InvM(i1) > 0)
				{
					Particles.P(i1) -= Multiplier * Particles.InvM(i1) * Delta;
				}
				if (Particles.InvM(i2))
				{
					Particles.P(i2) += Multiplier * Particles.InvM(i2) * Barys[ConstraintIndex] * Delta;
				}
				if (Particles.InvM(i3))
				{
					Particles.P(i3) += Multiplier * Particles.InvM(i3) * ((FReal)1. - Barys[ConstraintIndex]) * Delta;
				}
			}
		}
	}

private:
	FVec3 GetDelta(const FPBDParticles& Particles, const FReal Dt, const int32 InConstraintIndex, const FReal ExpStiffnessValue) const
	{
		const TVector<int32, 3>& Constraint = Constraints[InConstraintIndex];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];

		const FReal Bary = Barys[InConstraintIndex];
		const FReal PInvMass = Particles.InvM(i3) * ((FReal)1. - Bary) + Particles.InvM(i2) * Bary;
		if (Particles.InvM(i1) == (FReal)0. && PInvMass == (FReal)0.)
		{
			return FVec3((FReal)0.);
		}
		const FReal CombinedInvMass = PInvMass + Particles.InvM(i1);
		ensure(CombinedInvMass > (FReal)SMALL_NUMBER);

		const FVec3& P1 = Particles.P(i1);
		const FVec3& P2 = Particles.P(i2);
		const FVec3& P3 = Particles.P(i3);
		const FVec3 P = (P2 - P3) * Bary + P3;

		const FVec3 Difference = P1 - P;
		const FReal Distance = Difference.Size();
		if (UNLIKELY(Distance <= SMALL_NUMBER))
		{
			return FVec3((FReal)0.);
		}
		const FVec3 Direction = Difference / Distance;
		const FReal Offset = (Distance - Dists[InConstraintIndex]);

		FReal& Lambda = Lambdas[InConstraintIndex];
		const FReal Alpha = (FReal)XPBDAxialSpringMaxCompliance / (ExpStiffnessValue * Dt * Dt);

		const FReal DLambda = (Offset - Alpha * Lambda) / (CombinedInvMass + Alpha);
		const FVec3 Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	}

private:
	mutable TArray<FReal> Lambdas;
};

template<class T, int d>
using TXPBDAxialSpringConstraints UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FXPBDAxialSpringConstraints instead") = FXPBDAxialSpringConstraints;
}
