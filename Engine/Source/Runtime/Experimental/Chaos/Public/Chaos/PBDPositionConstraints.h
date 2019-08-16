// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDConstraintContainer.h"
#include "ParticleHandle.h"

namespace Chaos
{
template<class T, int d>
class TPBDPositionConstraints : public TPBDConstraintContainer<T, d>
{
  public:
	TPBDPositionConstraints(const T InStiffness = (T)1)
		: Stiffness(InStiffness)
	{}

	TPBDPositionConstraints(TArray<TVector<T, d>>&& Locations, TArray<TPBDRigidParticleHandle<T,d>*>&& InConstrainedParticles, const T InStiffness = (T)1)
		: Targets(MoveTemp(Locations)), ConstrainedParticles(MoveTemp(InConstrainedParticles)), Stiffness(InStiffness)
	{}

	virtual ~TPBDPositionConstraints() {}

	int32 NumConstraints() const
	{
		return ConstrainedParticles.Num();
	}

	TVector<TGeometryParticleHandle<T,d>*, 2> ConstraintParticles(int32 ConstraintIndex) const
	{ 
		return { ConstrainedParticles[ConstraintIndex], nullptr };
	}

	void UpdatePositionBasedState(const T Dt)
	{
	}

	void ApplyHelper(const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 ConstraintIndex : InConstraintIndices)
		{
			if (TPBDRigidParticleHandle<T, d>* PBDRigid = ConstrainedParticles[ConstraintIndex])
			{
				const TVector<T, d>& P1 = PBDRigid->P();
				const TVector<T, d>& P2 = Targets[ConstraintIndex];
				TVector<T, d> Difference = P1 - P2;
				PBDRigid->P() -= Stiffness * Difference;
			}
		}
	}

	void Apply(const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		ApplyHelper(Dt, InConstraintIndices);
	}

	void ApplyPushOut(const T Dt, const TArray<int32>& InConstraintIndices) const
	{
	}

	int32 Add(TGeometryParticleHandle<T,d>* Particle, const TVector<T, d>& Position)
	{
		int32 NewIndex = Targets.Num();
		Targets.Add(Position);
		ConstrainedParticles.Add(Particle);
		return NewIndex;
	}

	void Replace(const int32 Index, const TVector<T, d>& Position)
	{
		Targets[Index] = Position;
	}

	void RemoveConstraints(const TSet<TGeometryParticleHandle<T,d>*>& RemovedParticles)
	{
		// @todo(ccaulfield): constraint management
	}

private:
	TArray<TVector<T, d>> Targets;
	TArray<TPBDRigidParticleHandle<T,d>*> ConstrainedParticles;
	const T Stiffness;
};
}
