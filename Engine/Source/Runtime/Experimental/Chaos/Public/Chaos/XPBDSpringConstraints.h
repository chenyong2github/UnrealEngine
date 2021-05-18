// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos XPBD Spring Constraint"), STAT_XPBD_Spring, STATGROUP_Chaos);

namespace Chaos
{

// Stiffness is in N/CM^2, so it needs to be adjusted from the PBD stiffness ranging between [0,1]
static const double XPBDSpringMaxCompliance = 1e-7;  // Max stiffness: 1e+11 N/M^2 = 1e+7 N/CM^2 -> Max compliance: 1e-7 CM^2/N

class FXPBDSpringConstraints : public FPBDSpringConstraintsBase
{
	typedef FPBDSpringConstraintsBase Base;
	using Base::Constraints;
	using Base::Dists;
	using Base::Stiffness;

public:
	template<int32 Valence>
	FXPBDSpringConstraints(
		const FPBDParticles& Particles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const TArray<TVector<int32, Valence>>& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const FVec2& InStiffness,
		bool bTrimKinematicConstraints = false,
		typename TEnableIf<Valence >= 2 && Valence <= 4>::Type* = nullptr)
		: Base(Particles, ParticleOffset, ParticleCount, InConstraints, StiffnessMultipliers, InStiffness, bTrimKinematicConstraints)
	{
		Lambdas.Init((FReal)0., Constraints.Num());
	}

	virtual ~FXPBDSpringConstraints() {}

	void Init() const { for (FReal& Lambda : Lambdas) { Lambda = (FReal)0.; } }

	void Apply(FPBDParticles& Particles, const FReal Dt, const int32 ConstraintIndex, const FReal ExpStiffnessValue) const
	{
		const TVec2<int32>& Constraint = Constraints[ConstraintIndex];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const FVec3 Delta = GetDelta(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
		if (Particles.InvM(i1) > (FReal)0.)
		{
			Particles.P(i1) -= Particles.InvM(i1) * Delta;
		}
		if (Particles.InvM(i2) > (FReal)0.)
		{
			Particles.P(i2) += Particles.InvM(i2) * Delta;
		}
	}

	void Apply(FPBDParticles& Particles, const FReal Dt) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_Spring);

		if (!Stiffness.HasWeightMap())
		{
			const FReal ExpStiffnessValue = (FReal)Stiffness;
			for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
			{
				Apply(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
			}
		}
		else
		{
			for (int32 ConstraintIndex = 0; ConstraintIndex < Constraints.Num(); ++ConstraintIndex)
			{
				const FReal ExpStiffnessValue = Stiffness[ConstraintIndex];
				Apply(Particles, Dt, ConstraintIndex, ExpStiffnessValue);
			}
		}
	}

private:
	FVec3 GetDelta(const FPBDParticles& Particles, const FReal Dt, const int32 ConstraintIndex, const FReal ExpStiffnessValue) const
	{
		const TVec2<int32>& Constraint = Constraints[ConstraintIndex];

		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];

		if (Particles.InvM(i2) == (FReal)0. && Particles.InvM(i1) == (FReal)0.)
		{
			return FVec3((FReal)0.);
		}
		const FReal CombinedInvMass = Particles.InvM(i2) + Particles.InvM(i1);

		const FVec3& P1 = Particles.P(i1);
		const FVec3& P2 = Particles.P(i2);
		FVec3 Direction = P1 - P2;
		const FReal Distance = Direction.SafeNormalize();
		const FReal Offset = Distance - Dists[ConstraintIndex];

		FReal& Lambda = Lambdas[ConstraintIndex];
		const FReal Alpha = (FReal)XPBDSpringMaxCompliance / (ExpStiffnessValue * Dt * Dt);

		const FReal DLambda = (Offset - Alpha * Lambda) / (CombinedInvMass + Alpha);
		const FVec3 Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	}

private:
	mutable TArray<FReal> Lambdas;
};

}
