// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidSpringConstraintsBase.h"
#include "Chaos/PBDConstraintContainer.h"

namespace Chaos
{
template<class T, int d>
class TPBDRigidSpringConstraints2 : public TPBDRigidSpringConstraintsBase<T, d>, public TPBDConstraintContainer<T, d>
{
	typedef TPBDRigidSpringConstraintsBase<T, d> Base;

	using Base::Constraints;
	using Base::Distances;

public:
	using FConstrainedParticlePair = TVector<TGeometryParticleHandle<T, d>*, 2>;


	TPBDRigidSpringConstraints2(const T InStiffness = (T)1)
	    : TPBDRigidSpringConstraintsBase<T, d>(InStiffness) 
	{}

	TPBDRigidSpringConstraints2(const TArray<TVector<T, 3>>& Locations0, const TArray<TVector<T, 3>>& Locations1, TArray<FConstrainedParticlePair>&& InConstraints, const T InStiffness = (T)1)
	    : TPBDRigidSpringConstraintsBase<T, d>(Locations0, Locations1, MoveTemp(InConstraints), InStiffness)
	{}

	virtual ~TPBDRigidSpringConstraints2() {}

	void UpdatePositionBasedState(const T Dt)
	{
	}

	void ApplyHelper(const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		for (int32 ConstraintIndex : InConstraintIndices)
		{
			const FConstrainedParticlePair& Constraint = Constraints[ConstraintIndex];

			TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint[0]->AsDynamic();
			TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint[1]->AsDynamic();
			check((PBDRigid0 && PBDRigid1 && PBDRigid0->Island() == PBDRigid1->Island()) || (!PBDRigid0 && PBDRigid1) || (PBDRigid0 && PBDRigid1));

			// @todo(ccaulfield): see if we can eliminate the need for all these ifs
			const TRotation<T, d> & Q0 = PBDRigid0 ? PBDRigid0->Q() : Constraint[0]->R();
			const TRotation<T, d> & Q1 = PBDRigid1 ? PBDRigid1->Q() : Constraint[1]->R();
			const TVector<T, d> & P0 = PBDRigid0 ? PBDRigid0->P() : Constraint[0]->X();
			const TVector<T, d> & P1 = PBDRigid1 ? PBDRigid1->P() : Constraint[1]->X();

			const TVector<T, d> WorldSpaceX1 = Q0.RotateVector(Distances[ConstraintIndex][0]) + P0;
			const TVector<T, d> WorldSpaceX2 = Q1.RotateVector(Distances[ConstraintIndex][1]) + P1;
			const PMatrix<T, d, d> WorldSpaceInvI1 = PBDRigid0 ? (PBDRigid0->Q() * FMatrix::Identity) * PBDRigid0->InvI() * (PBDRigid0->Q() * FMatrix::Identity).GetTransposed() : PMatrix<T, d, d>(0);
			const PMatrix<T, d, d> WorldSpaceInvI2 = PBDRigid1 ? (PBDRigid1->Q() * FMatrix::Identity) * PBDRigid1->InvI() * (PBDRigid1->Q() * FMatrix::Identity).GetTransposed() : PMatrix<T, d, d>(0);
			const TVector<T, d> Delta = Base::GetDelta(WorldSpaceX1, WorldSpaceX2, ConstraintIndex);

			if (PBDRigid0)
			{
				const TVector<T, d> Radius = WorldSpaceX1 - PBDRigid0->P();
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

	CHAOS_API void ApplyPushOut(const T Dt, const TArray<int32>& InConstraintIndices)
	{
	}
};
}
