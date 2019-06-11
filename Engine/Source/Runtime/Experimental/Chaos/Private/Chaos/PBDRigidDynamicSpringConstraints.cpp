// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidDynamicSpringConstraints.h"

using namespace Chaos;

template<class T, int d>
void TPBDRigidDynamicSpringConstraints<T, d>::ApplyHelper(TPBDRigidParticles<T, d>& InParticles, const T Dt, const TArray<int32>& InConstraintIndices) const
{
	for (int32 ConstraintIndex : InConstraintIndices)
	{
		const TVector<int32, 2>& Constraint = Constraints[ConstraintIndex];

		int32 ConstraintInnerIndex1 = Constraint[0];
		int32 ConstraintInnerIndex2 = Constraint[1];

		check(InParticles.Island(ConstraintInnerIndex1) == InParticles.Island(ConstraintInnerIndex2) || InParticles.Island(ConstraintInnerIndex1) == -1 || InParticles.Island(ConstraintInnerIndex2) == -1);

		const int32 NumSprings = SpringDistances[ConstraintIndex].Num();
		const PMatrix<T, d, d> WorldSpaceInvI1 = (InParticles.Q(ConstraintInnerIndex1) * FMatrix::Identity) * InParticles.InvI(ConstraintInnerIndex1) * (InParticles.Q(ConstraintInnerIndex1) * FMatrix::Identity).GetTransposed();
		const PMatrix<T, d, d> WorldSpaceInvI2 = (InParticles.Q(ConstraintInnerIndex2) * FMatrix::Identity) * InParticles.InvI(ConstraintInnerIndex2) * (InParticles.Q(ConstraintInnerIndex2) * FMatrix::Identity).GetTransposed();
		for (int32 SpringIndex = 0; SpringIndex < NumSprings; ++SpringIndex)
		{
			const TVector<T, d> WorldSpaceX1 = InParticles.Q(ConstraintInnerIndex1).RotateVector(Distances[ConstraintIndex][SpringIndex][0]) + InParticles.P(ConstraintInnerIndex1);
			const TVector<T, d> WorldSpaceX2 = InParticles.Q(ConstraintInnerIndex2).RotateVector(Distances[ConstraintIndex][SpringIndex][1]) + InParticles.P(ConstraintInnerIndex2);
			const TVector<T, d> Delta = Base::GetDelta(InParticles, WorldSpaceX1, WorldSpaceX2, ConstraintIndex, SpringIndex);

			if (InParticles.InvM(ConstraintInnerIndex1) > 0)
			{
				const TVector<T, d> Radius = WorldSpaceX1 - InParticles.P(ConstraintInnerIndex1);
				InParticles.P(ConstraintInnerIndex1) += InParticles.InvM(ConstraintInnerIndex1) * Delta;
				InParticles.Q(ConstraintInnerIndex1) += TRotation<T, d>(WorldSpaceInvI1 * TVector<T, d>::CrossProduct(Radius, Delta), 0.f) * InParticles.Q(ConstraintInnerIndex1) * T(0.5);
				InParticles.Q(ConstraintInnerIndex1).Normalize();
			}

			if (InParticles.InvM(ConstraintInnerIndex2) > 0)
			{
				const TVector<T, d> Radius = WorldSpaceX2 - InParticles.P(ConstraintInnerIndex2);
				InParticles.P(ConstraintInnerIndex2) -= InParticles.InvM(ConstraintInnerIndex2) * Delta;
				InParticles.Q(ConstraintInnerIndex2) += TRotation<T, d>(WorldSpaceInvI2 * TVector<T, d>::CrossProduct(Radius, -Delta), 0.f) * InParticles.Q(ConstraintInnerIndex2) * T(0.5);
				InParticles.Q(ConstraintInnerIndex2).Normalize();
			}
		}
	}
}

namespace Chaos
{
	template class TPBDRigidDynamicSpringConstraints<float, 3>;
}
