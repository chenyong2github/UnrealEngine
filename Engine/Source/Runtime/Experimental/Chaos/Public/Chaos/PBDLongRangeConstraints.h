// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDLongRangeConstraintsBase.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDConstraintContainer.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Long Range Constraint"), STAT_PBD_LongRange, STATGROUP_Chaos);

namespace Chaos
{
template<class T, int d>
class TPBDLongRangeConstraints : public TPBDLongRangeConstraintsBase<T, d>, public FPBDConstraintContainer
{
	typedef TPBDLongRangeConstraintsBase<T, d> Base;
	using Base::MConstraints;

  public:
	TPBDLongRangeConstraints(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments = 1, const T Stiffness = (T)1)
	    : TPBDLongRangeConstraintsBase<T, d>(InParticles, PointToNeighbors, NumberOfAttachments, Stiffness) {}
	virtual ~TPBDLongRangeConstraints() {}


	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, int32 Index) const
	{
		const auto& Constraint = MConstraints[Index];
		int32 i2 = Constraint[Constraint.Num() - 1];
		check(InParticles.InvM(i2) > 0);
		InParticles.P(i2) += Base::GetDelta(InParticles, Index);
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const 
	{
		SCOPE_CYCLE_COUNTER(STAT_PBD_LongRange);
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			Apply(InParticles, Dt, i);
		}
	}

	void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		SCOPE_CYCLE_COUNTER(STAT_PBD_LongRange);
		for (int32 i : InConstraintIndices)
		{
			Apply(InParticles, Dt, i);
		}
	}
};
}
