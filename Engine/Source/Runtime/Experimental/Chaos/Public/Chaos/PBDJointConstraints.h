// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDJointConstraintsBase.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
template<class T, int d>
class CHAOS_API TPBDJointConstraints : public TPBDJointConstraintsBase<T, d>, public TPBDConstraintContainer<T, d>
{
	typedef TPBDJointConstraintsBase<T, d> Base;

  public:
	using Base::AddConstraint;
	using Base::ApplySingle;
	using Base::ConstraintParticles;
	using Base::NumConstraints;
	using Base::RemoveConstraints;

	TPBDJointConstraints(const T InStiffness = (T)1)
	    : TPBDJointConstraintsBase<T, d>(InStiffness) 
	{}

	TPBDJointConstraints(const TArray<TVector<T, 3>>& Locations, TArray<TVector<TGeometryParticleHandle<T,d>*, 2>>&& InConstraints, const T InStiffness = (T)1)
	    : TPBDJointConstraintsBase<T, d>(Locations, MoveTemp(InConstraints), InStiffness) 
	{}

	virtual ~TPBDJointConstraints()
	{}


	void UpdatePositionBasedState(const T Dt)
	{
	}

	void Apply(const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 ConstraintIndex : InConstraintIndices)
		{
			ApplySingle(Dt, ConstraintIndex);
			}
	}

	void ApplyPushOut(const T Dt, const TArray<int32>& InConstraintIndices) const
	{
	}
};
}
