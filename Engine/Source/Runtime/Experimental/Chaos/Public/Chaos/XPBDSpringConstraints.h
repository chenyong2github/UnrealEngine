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

class FXPBDSpringConstraints : public FPBDSpringConstraintsBase, public FPBDConstraintContainer
{
	typedef FPBDSpringConstraintsBase Base;
	using Base::MConstraints;
	using Base::MDists;
	using Base::MStiffness;

public:
	FXPBDSpringConstraints(const FReal Stiffness = (FReal)1.)
	    : FPBDSpringConstraintsBase(Stiffness)
	{}

	FXPBDSpringConstraints(const FDynamicParticles& InParticles, TArray<TVec2<int32>>&& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		: FPBDSpringConstraintsBase(InParticles, MoveTemp(Constraints), Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init((FReal)0., MConstraints.Num());
	}

	FXPBDSpringConstraints(const TRigidParticles<FReal, 3>& InParticles, TArray<TVec2<int32>>&& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		: FPBDSpringConstraintsBase(InParticles, MoveTemp(Constraints), Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init((FReal)0., MConstraints.Num());
	}

	FXPBDSpringConstraints(const FDynamicParticles& InParticles, const TArray<TVec3<int32>>& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		: FPBDSpringConstraintsBase(InParticles, Constraints, Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init((FReal)0., MConstraints.Num());
	}

	FXPBDSpringConstraints(const FDynamicParticles& InParticles, const TArray<TVec4<int32>>& Constraints, const FReal Stiffness = (FReal)1., bool bStripKinematicConstraints = false)
		: FPBDSpringConstraintsBase(InParticles, Constraints, Stiffness, bStripKinematicConstraints)
	{
		MLambdas.Init((FReal)0., MConstraints.Num());
	}

	virtual ~FXPBDSpringConstraints() {}

	const TArray<TVec2<int32>>& GetConstraints() const { return MConstraints; }
	TArray<TVec2<int32>>& GetConstraints() { return MConstraints; }
	TArray<TVec2<int32>>& Constraints() { return MConstraints; }

	void Init() const { for (FReal& Lambda : MLambdas) { Lambda = (FReal)0.; } }

	void Apply(FPBDParticles& InParticles, const FReal Dt, const int32 InConstraintIndex) const
	{
		const int32 i = InConstraintIndex;
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];
			const FVec3 Delta = GetDelta(InParticles, Dt, i);
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

	void Apply(FPBDParticles& InParticles, const FReal Dt) const
	{
		SCOPE_CYCLE_COUNTER(STAT_XPBD_Spring);
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

private:
	inline FVec3 GetDelta(const FPBDParticles& InParticles, const FReal Dt, const int32 InConstraintIndex) const
	{
		const TVec2<int32>& Constraint = MConstraints[InConstraintIndex];

		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];

		if (InParticles.InvM(i2) == 0 && InParticles.InvM(i1) == 0)
			return FVec3(0.);
		const FReal CombinedInvMass = InParticles.InvM(i2) + InParticles.InvM(i1);

		const FVec3& P1 = InParticles.P(i1);
		const FVec3& P2 = InParticles.P(i2);
		FVec3 Direction = P1 - P2;
		const FReal Distance = Direction.SafeNormalize();
		const FReal Offset = Distance - MDists[InConstraintIndex];

		FReal& Lambda = MLambdas[InConstraintIndex];
		const FReal Alpha = (FReal)XPBDSpringMaxCompliance / (MStiffness * Dt * Dt);

		const FReal DLambda = (Offset - Alpha * Lambda) / (CombinedInvMass + Alpha);
		const FVec3 Delta = DLambda * Direction;
		Lambda += DLambda;

		return Delta;
	}

private:
	mutable TArray<FReal> MLambdas;
};
}
