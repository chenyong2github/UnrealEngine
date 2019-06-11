// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDRigidDynamicSpringConstraintsBase.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
template<class T, int d>
class TPBDRigidDynamicSpringConstraints : public TPBDRigidDynamicSpringConstraintsBase<T, d>, public TPBDConstraintContainer<T, d>
{
	typedef TPBDRigidDynamicSpringConstraintsBase<T, d> Base;

	using Base::Constraints;
	using Base::Distances;
	using Base::SpringDistances;

public:
	using Base::UpdatePositionBasedState;

	TPBDRigidDynamicSpringConstraints(const T InStiffness = (T)1)
	    : TPBDRigidDynamicSpringConstraintsBase<T, d>(InStiffness) 
	{}

	TPBDRigidDynamicSpringConstraints(TArray<TVector<int32, 2>>&& InConstraints, const T InCreationThreshold = (T)1, const int32 InMaxSprings = 1, const T InStiffness = (T)1)
	    : TPBDRigidDynamicSpringConstraintsBase<T, d>(MoveTemp(InConstraints), InCreationThreshold, InMaxSprings, InStiffness)
	{}

	virtual ~TPBDRigidDynamicSpringConstraints() {}

	CHAOS_API void UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const T Dt)
	{
		Base::UpdatePositionBasedState(InParticles);
	}

	// @todo(mlentine): Why is this needed?
	CHAOS_API void ApplyHelper(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const;

	CHAOS_API void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		ApplyHelper(InParticles, Dt, InConstraintIndices);
	}

	CHAOS_API void ApplyPushOut(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices)
	{

	}
};
}
