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
	using Base::Constraints;
	using Base::Distances;

  public:
	TPBDJointConstraints(const T InStiffness = (T)1)
	    : TPBDJointConstraintsBase<T, d>(InStiffness) 
	{}

	TPBDJointConstraints(const TRigidParticles<T, d>& InParticles, const TArray<TVector<T, 3>>& Locations, TArray<TVector<int32, 2>>&& InConstraints, const T InStiffness = (T)1)
	    : TPBDJointConstraintsBase<T, d>(InParticles, Locations, MoveTemp(InConstraints), InStiffness) 
	{}

	virtual ~TPBDJointConstraints()
	{}

	TArray<TVector<int32, 2>>& GetConstraints()
	{
		return Constraints;
	}

	int32 NumConstraints() const
	{
		return Constraints.Num();
	}

	TVector<int32, 2> ConstraintParticleIndices(int32 ConstraintIndex) const
	{
		return Constraints[ConstraintIndex];
	}

	void UpdatePositionBasedState(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, const T Dt)
	{
	}

	void ApplyHelper(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		// @todo(ccaulfield): should be an option. Per-constraint or per-container?
		const bool bApplyProjection = true;

		for (int32 ConstraintIndex : InConstraintIndices)
		{
			const TVector<int32, 2>& Constraint = Constraints[ConstraintIndex];
			int32 ConstraintInnerIndex1 = Constraint[0];
			int32 ConstraintInnerIndex2 = Constraint[1];

			check(InParticles.Island(ConstraintInnerIndex1) == InParticles.Island(ConstraintInnerIndex2) || InParticles.Island(ConstraintInnerIndex1) == INDEX_NONE || InParticles.Island(ConstraintInnerIndex2) == INDEX_NONE);

			const TVector<T, d> WorldSpaceX1 = InParticles.Q(ConstraintInnerIndex1).RotateVector(Distances[ConstraintIndex][0]) + InParticles.P(ConstraintInnerIndex1);
			const TVector<T, d> WorldSpaceX2 = InParticles.Q(ConstraintInnerIndex2).RotateVector(Distances[ConstraintIndex][1]) + InParticles.P(ConstraintInnerIndex2);
			const PMatrix<T, d, d> WorldSpaceInvI1 = (InParticles.Q(ConstraintInnerIndex1) * FMatrix::Identity) * InParticles.InvI(ConstraintInnerIndex1) * (InParticles.Q(ConstraintInnerIndex1) * FMatrix::Identity).GetTransposed();
			const PMatrix<T, d, d> WorldSpaceInvI2 = (InParticles.Q(ConstraintInnerIndex2) * FMatrix::Identity) * InParticles.InvI(ConstraintInnerIndex2) * (InParticles.Q(ConstraintInnerIndex2) * FMatrix::Identity).GetTransposed();
			const TVector<T, d> Delta = Base::GetDelta(InParticles, WorldSpaceX1, WorldSpaceX2, WorldSpaceInvI1, WorldSpaceInvI2, ConstraintIndex);

			if (InParticles.InvM(ConstraintInnerIndex1) > 0)
			{
				const TVector<T, d> Radius = WorldSpaceX1 - InParticles.P(ConstraintInnerIndex1);
				const TVector<T, d> Dp = InParticles.InvM(ConstraintInnerIndex1) * Delta;
				const TRotation<T, d> Dq = TRotation<T, d>(WorldSpaceInvI1 * TVector<T, d>::CrossProduct(Radius, Delta), 0.f)* InParticles.Q(ConstraintInnerIndex1)* T(0.5);
				InParticles.P(ConstraintInnerIndex1) += Dp;
				InParticles.Q(ConstraintInnerIndex1) += Dq;
				InParticles.Q(ConstraintInnerIndex1).Normalize();
			}

			if (InParticles.InvM(ConstraintInnerIndex2) > 0)
			{
				const TVector<T, d> Radius = WorldSpaceX2 - InParticles.P(ConstraintInnerIndex2);
				const TVector<T, d> Dp = InParticles.InvM(ConstraintInnerIndex2) * Delta;
				const TRotation<T, d> Dq = TRotation<T, d>(WorldSpaceInvI2 * TVector<T, d>::CrossProduct(Radius, -Delta), 0.f) * InParticles.Q(ConstraintInnerIndex2) * T(0.5);
				InParticles.P(ConstraintInnerIndex2) -= Dp;
				InParticles.Q(ConstraintInnerIndex2) += Dq;
				InParticles.Q(ConstraintInnerIndex2).Normalize();
			}

			if (bApplyProjection)
			{
				const TVector<T, d> WorldSpaceX12 = InParticles.Q(ConstraintInnerIndex1).RotateVector(Distances[ConstraintIndex][0]) + InParticles.P(ConstraintInnerIndex1);
				const TVector<T, d> WorldSpaceX22 = InParticles.Q(ConstraintInnerIndex2).RotateVector(Distances[ConstraintIndex][1]) + InParticles.P(ConstraintInnerIndex2);
				const TVector<T, d> Delta2 = (WorldSpaceX22 - WorldSpaceX12) / (InParticles.InvM(ConstraintInnerIndex1) + InParticles.InvM(ConstraintInnerIndex2));
				if (InParticles.InvM(ConstraintInnerIndex1) > 0)
				{
					const TVector<T, d> Dp = InParticles.InvM(ConstraintInnerIndex1) * Delta2;
					InParticles.P(ConstraintInnerIndex1) += Dp;
				}
				if (InParticles.InvM(ConstraintInnerIndex2) > 0)
				{
					const TVector<T, d> Dp = InParticles.InvM(ConstraintInnerIndex2) * Delta2;
					InParticles.P(ConstraintInnerIndex2) -= Dp;
				}
			}
		}
	}

	void Apply(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
		ApplyHelper(InParticles, Dt, InConstraintIndices);
	}

	void ApplyPushOut(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
	{
	}

	void RemoveConstraints(const TSet<uint32>& RemovedParticles)
	{
		// @todo(ccaulfield): constraint management
	}
};
}
