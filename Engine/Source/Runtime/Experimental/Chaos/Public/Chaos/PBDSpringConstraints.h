// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDSpringConstraintsBase.h"
#include "Chaos/PBDConstraintContainer.h"
#include "ChaosStats.h"

#include "Templates/EnableIf.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Spring Constraint"), STAT_PBD_Spring, STATGROUP_Chaos);

namespace Chaos
{
template<class T, int32 d>
class TPBDSpringConstraints : public TPBDSpringConstraintsBase<T, d>, public FPBDConstraintContainer
{
	typedef TPBDSpringConstraintsBase<T, d> Base;
	using Base::MConstraints;

  public:
	TPBDSpringConstraints(const T Stiffness = (T)1)
	    : TPBDSpringConstraintsBase<T, d>(Stiffness) 
	{}

	TPBDSpringConstraints(const TDynamicParticles<T, d>& InParticles, TArray<TVector<int32, 2>>&& Constraints, const T Stiffness = (T)1)
	    : TPBDSpringConstraintsBase<T, d>(InParticles, MoveTemp(Constraints), Stiffness) 
	{}
	TPBDSpringConstraints(const TRigidParticles<T, d>& InParticles, TArray<TVector<int32, 2>>&& Constraints, const T Stiffness = (T)1)
	    : TPBDSpringConstraintsBase<T, d>(InParticles, MoveTemp(Constraints), Stiffness) 
	{}

	TPBDSpringConstraints(const TDynamicParticles<T, d>& InParticles, const TArray<TVector<int32, 3>>& Constraints, const T Stiffness = (T)1)
	    : TPBDSpringConstraintsBase<T, d>(InParticles, Constraints, Stiffness) 
	{}

	TPBDSpringConstraints(const TDynamicParticles<T, d>& InParticles, const TArray<TVector<int32, 4>>& Constraints, const T Stiffness = (T)1)
	    : TPBDSpringConstraintsBase<T, d>(InParticles, Constraints, Stiffness) 
	{}

	virtual ~TPBDSpringConstraints() {}

	const TArray<TVector<int32, 2>>& GetConstraints() const { return MConstraints; }
	TArray<TVector<int32, 2>>& GetConstraints() { return MConstraints; }
	TArray<TVector<int32, 2>>& Constraints() { return MConstraints; }

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 InConstraintIndex) const
	{
		const int32 i = InConstraintIndex;
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];
			auto Delta = Base::GetDelta(InParticles, i);
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
		SCOPE_CYCLE_COUNTER(STAT_PBD_Spring);
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		SCOPE_CYCLE_COUNTER(STAT_PBD_Spring);
		for (int32 i : InConstraintIndices)
		{
			const auto& Constraint = MConstraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];
			check(InParticles.Island(i1) == InParticles.Island(i2) || InParticles.Island(i1) == INDEX_NONE || InParticles.Island(i2) == INDEX_NONE);
			Apply(InParticles, Dt, i);
		}
	}
};
}
