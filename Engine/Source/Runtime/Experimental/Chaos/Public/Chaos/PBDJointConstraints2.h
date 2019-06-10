// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDJointConstraintsBase2.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
template<class T, int d>
class CHAOS_API TPBDJointConstraints2 : public TPBDJointConstraintsBase2<T, d>, public TPBDConstraintContainer<T, d>
{
	typedef TPBDJointConstraintsBase2<T, d> Base;
	using Base::Constraints;
	using Base::Distances;

  public:
	TPBDJointConstraints2(const T InStiffness = (T)1)
	    : TPBDJointConstraintsBase2<T, d>(InStiffness) 
	{}

	TPBDJointConstraints2(const TArray<TVector<T, 3>>& Locations, TArray<TVector<TGeometryParticleHandle<T,d>*, 2>>&& InConstraints, const T InStiffness = (T)1)
	    : TPBDJointConstraintsBase2<T, d>(Locations, MoveTemp(InConstraints), InStiffness) 
	{}

	virtual ~TPBDJointConstraints2()
	{}

	TArray<TVector<TGeometryParticleHandle<T,d>*, 2>>& GetConstraints()
	{
		return Constraints;
	}

	int32 NumConstraints() const
	{
		return Constraints.Num();
	}

	TVector<TGeometryParticleHandle<T, d>*, 2> ConstraintParticles(int32 ConstraintIndex) const
	{
		return Constraints[ConstraintIndex];
	}

	void UpdatePositionBasedState(const T Dt)
	{
	}

	void ApplyHelper(const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 ConstraintIndex : InConstraintIndices)
		{
			const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = Constraints[ConstraintIndex];
			TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint[0]->ToDynamic();
			TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint[1]->ToDynamic();
			check((PBDRigid0 && PBDRigid1 && PBDRigid0->Island() == PBDRigid1->Island()) || (!PBDRigid0 && PBDRigid1) || (PBDRigid0 && PBDRigid1));
			
			const TRotation<T, d>& Q0 = PBDRigid0 ? PBDRigid0->Q() : Constraint[0]->R();
			const TRotation<T, d>& Q1 = PBDRigid1 ? PBDRigid1->Q() : Constraint[1]->R();
			const TVector<T, d>& P0 = PBDRigid0 ? PBDRigid0->P() : Constraint[0]->X();
			const TVector<T, d>& P1 = PBDRigid1 ? PBDRigid1->P() : Constraint[1]->X();

			const TVector<T, d> WorldSpaceX1 = Q0.RotateVector(Distances[ConstraintIndex][0]) + P0;
			const TVector<T, d> WorldSpaceX2 = Q1.RotateVector(Distances[ConstraintIndex][1]) + P1;
			const PMatrix<T, d, d> WorldSpaceInvI1 = PBDRigid0 ? (PBDRigid0->Q() * FMatrix::Identity) * PBDRigid0->InvI() * (PBDRigid0->Q() * FMatrix::Identity).GetTransposed() : PMatrix<T, d, d>(0);
			const PMatrix<T, d, d> WorldSpaceInvI2 = PBDRigid1 ? (PBDRigid1->Q() * FMatrix::Identity) * PBDRigid1->InvI() * (PBDRigid1->Q() * FMatrix::Identity).GetTransposed() : PMatrix<T, d, d>(0);
			const TVector<T, d> Delta = Base::GetDelta(WorldSpaceX1, WorldSpaceX2, WorldSpaceInvI1, WorldSpaceInvI2, ConstraintIndex);

			if (PBDRigid0)
			{
				const TVector<T, d> Radius = WorldSpaceX1 - PBDRigid0->P();;
				PBDRigid0->P() += PBDRigid0->InvM() * Delta;
				PBDRigid0->Q() += TRotation<T, d>(WorldSpaceInvI1 * TVector<T, d>::CrossProduct(Radius, Delta), 0.f) * PBDRigid0->Q() * T(0.5);
				PBDRigid0->Q().Normalize();
			}

			if (PBDRigid1)
			{
				const TVector<T, d> Radius = WorldSpaceX2 - PBDRigid1->P();
				PBDRigid1->P() -= PBDRigid1->InvM() * Delta;
				PBDRigid1->Q() += TRotation<T, d>(WorldSpaceInvI2 * TVector<T, d>::CrossProduct(Radius, -Delta), 0.f) * PBDRigid1->Q() * T(0.5);
				PBDRigid1->Q().Normalize();
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

	void RemoveConstraints(const TSet<TGeometryParticleHandle<T,d>*>& RemovedParticles)
	{
		// @todo(ccaulfield): constraint management
	}
};
}
