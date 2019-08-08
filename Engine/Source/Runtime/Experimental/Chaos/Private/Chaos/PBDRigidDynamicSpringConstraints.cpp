// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidDynamicSpringConstraints.h"

using namespace Chaos;

template<class T, int d>
void TPBDRigidDynamicSpringConstraints<T, d>::ApplyHelper(const T Dt, const TArray<int32>& InConstraintIndices) const
{
 	for (int32 ConstraintIndex : InConstraintIndices)
	{
		TGeometryParticleHandle<T, d>* Static0 = Constraints[ConstraintIndex][0];
		TGeometryParticleHandle<T, d>* Static1 = Constraints[ConstraintIndex][1];
		TPBDRigidParticleHandle<T, d>* PBDRigid0 = Static0->AsDynamic();
		TPBDRigidParticleHandle<T, d>* PBDRigid1 = Static1->AsDynamic();
		check(PBDRigid0 || PBDRigid1);
		check(!PBDRigid0 || !PBDRigid1 || (PBDRigid0->Island() == PBDRigid1->Island()));

		TRotation<T, d>& Q0 = PBDRigid0 ? PBDRigid0->Q() : Static0->R();
		TRotation<T, d>& Q1 = PBDRigid1 ? PBDRigid1->Q() : Static1->R();
		TVector<T, d>& P0 = PBDRigid0 ? PBDRigid0->P() : Static0->X();
		TVector<T, d>& P1 = PBDRigid1 ? PBDRigid1->P() : Static1->X();

		const int32 NumSprings = SpringDistances[ConstraintIndex].Num();
		const PMatrix<T, d, d> WorldSpaceInvI1 = PBDRigid0? (Q0 * FMatrix::Identity) * PBDRigid0->InvI() * (Q0 * FMatrix::Identity).GetTransposed() : PMatrix<T, d, d>(0);
		const PMatrix<T, d, d> WorldSpaceInvI2 = PBDRigid1 ? (Q1 * FMatrix::Identity) * PBDRigid1->InvI() * (Q1 * FMatrix::Identity).GetTransposed() : PMatrix<T, d, d>(0);;
		for (int32 SpringIndex = 0; SpringIndex < NumSprings; ++SpringIndex)
		{
			const TVector<T, d>& Distance0 = Distances[ConstraintIndex][SpringIndex][0];
			const TVector<T, d>& Distance1 = Distances[ConstraintIndex][SpringIndex][1];
			const TVector<T, d> WorldSpaceX1 = Q0.RotateVector(Distance0) + P0;
			const TVector<T, d> WorldSpaceX2 = Q1.RotateVector(Distance1) + P1;
			const TVector<T, d> Delta = Base::GetDelta(WorldSpaceX1, WorldSpaceX2, ConstraintIndex, SpringIndex);

			if (PBDRigid0)
			{
				const TVector<T, d> Radius = WorldSpaceX1 - P0;
				P0 += PBDRigid0->InvM() * Delta;
				Q0 += TRotation<T, d>(WorldSpaceInvI1 * TVector<T, d>::CrossProduct(Radius, Delta), 0.f) * Q0 * T(0.5);
				Q0.Normalize();
			}

			if (PBDRigid1)
			{
				const TVector<T, d> Radius = WorldSpaceX2 - P1;
				P1 -= PBDRigid1->InvM() * Delta;
				Q1 += TRotation<T, d>(WorldSpaceInvI2 * TVector<T, d>::CrossProduct(Radius, -Delta), 0.f) * Q1 * T(0.5);
				Q1.Normalize();
			}
		}
	}
}

namespace Chaos
{
	template class TPBDRigidDynamicSpringConstraints<float, 3>;
}
