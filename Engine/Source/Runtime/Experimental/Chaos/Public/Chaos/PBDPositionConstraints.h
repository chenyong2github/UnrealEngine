// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
template<class T, int d>
class TPBDPositionConstraints : public TPBDConstraintContainer<T, d>
{
  public:
	TPBDPositionConstraints(const T InStiffness = (T)1)
		: Stiffness(InStiffness)
	{}

	TPBDPositionConstraints(const TRigidParticles<T, d>& InParticles, TArray<TVector<T, d>>&& Locations, TArray<int32>&& InConstraints, const T InStiffness = (T)1)
		: Targets(MoveTemp(Locations)), Constraints(MoveTemp(InConstraints)), Stiffness(InStiffness)
	{}

	virtual ~TPBDPositionConstraints() {}

	int32 NumConstraints() const
	{
		return Constraints.Num();
	}

	TVector<int32, 2> ConstraintParticleIndices(int32 ConstraintIndex) const
	{ 
		return { Constraints[ConstraintIndex], INDEX_NONE };
	}

	void UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const T Dt)
	{
	}

	void ApplyHelper(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 ConstraintIndex : InConstraintIndices)
		{
			const int32 Constraint = Constraints[ConstraintIndex];

			if (InParticles.InvM(Constraint) == 0)
			{
				continue;
			}
			const TVector<T, d>& P1 = InParticles.P(Constraint);
			const TVector<T, d>& P2 = Targets[ConstraintIndex];
			TVector<T, d> Difference = P1 - P2;
			InParticles.P(Constraint) -= Stiffness * Difference;
		}
	}

	void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		ApplyHelper(InParticles, Dt, InConstraintIndices);
	}

	void ApplyPushOut(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
	}

	int32 Add(const int32 Index, const TVector<T, d>& Position)
	{
		int32 NewIndex = Targets.Num();
		Targets.Add(Position);
		Constraints.Add(Index);
		return NewIndex;
	}

	void Replace(const int32 Index, const TVector<T, d>& Position)
	{
		Targets[Index] = Position;
	}

	void RemoveConstraints(const TSet<uint32>& RemovedParticles)
	{
		// @todo(ccaulfield): constraint management
	}

private:
	TArray<TVector<T, d>> Targets;
	TArray<int32> Constraints;
	const T Stiffness;
};
}
