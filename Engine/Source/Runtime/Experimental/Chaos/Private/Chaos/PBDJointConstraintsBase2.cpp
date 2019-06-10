// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraintsBase2.h"

using namespace Chaos;

// @todo(mlentine): This should be in a utility class somewhere
template<class T>
PMatrix<T, 3, 3> ComputeJointFactorMatrix2(const TVector<T, 3>& V, const PMatrix<T, 3, 3>& M, const T& Im)
{
	// Rigid objects rotational contribution to the impulse.
	// Vx*M*VxT+Im
	check(Im > FLT_MIN)
	return PMatrix<T, 3, 3>(
		-V[2] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]) + V[1] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]) + Im,
		V[2] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) - V[0] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]),
		-V[1] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) + V[0] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]),
		V[2] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) - V[0] * (V[2] * M.M[2][0] - V[0] * M.M[2][2]) + Im,
		-V[1] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) + V[0] * (V[2] * M.M[1][0] - V[0] * M.M[2][1]),
		-V[1] * (-V[1] * M.M[0][0] + V[0] * M.M[1][0]) + V[0] * (-V[1] * M.M[1][0] + V[0] * M.M[1][1]) + Im);
}

template<class T, int d>
void TPBDJointConstraintsBase2<T, d>::UpdateDistanceInternal(const TVector<T, d>& InLocation, const int32 InConstraintIndex)
{
	const TGeometryParticleHandle<T,d>* Particle0 = Constraints[InConstraintIndex][0];
	const TGeometryParticleHandle<T, d>* Particle1 = Constraints[InConstraintIndex][1];
	Distances[InConstraintIndex][0] = Particle0->R().Inverse().RotateVector(InLocation - Particle0->X());
	Distances[InConstraintIndex][1] = Particle1->R().Inverse().RotateVector(InLocation - Particle1->X());
}

template<class T, int d>
void TPBDJointConstraintsBase2<T, d>::UpdateDistance(const TVector<T, d>& InLocation, const int32 InConstraintIndex)
{
	Distances.SetNum(Constraints.Num());
	UpdateDistanceInternal(InLocation, InConstraintIndex);
}

template<class T, int d>
void TPBDJointConstraintsBase2<T, d>::UpdateDistances(const TArray<TVector<T, d>>& InLocations)
{
	const int32 NumConstraints = Constraints.Num();
	Distances.SetNum(NumConstraints);
	for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints; ++ConstraintIndex)
	{
		UpdateDistanceInternal(InLocations[ConstraintIndex], ConstraintIndex);
	}
}

template<class T, int d>
TVector<T, d> TPBDJointConstraintsBase2<T, d>::GetDelta(const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, const PMatrix<T, d, d>& WorldSpaceInvI1, const PMatrix<T, d, d>& WorldSpaceInvI2, int32 ConstraintIndex) const
{
	const TVector<TGeometryParticleHandle<T,d>*, 2>& Constraint = Constraints[ConstraintIndex];
	const TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint[0]->ToDynamic();
	const TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint[1]->ToDynamic();

	if (!PBDRigid0 && !PBDRigid1)
	{
		return TVector<T, d>(0);
	}

	PMatrix<T, d, d> Factor =
		(PBDRigid0 ? ComputeJointFactorMatrix2(WorldSpaceX1 - PBDRigid0->P(), WorldSpaceInvI1, PBDRigid0->InvM()) : PMatrix<T, d, d>(0)) +
		(PBDRigid1 ? ComputeJointFactorMatrix2(WorldSpaceX2 - PBDRigid1->P(), WorldSpaceInvI2, PBDRigid1->InvM()) : PMatrix<T, d, d>(0));
	PMatrix<T, d, d> FactorInv = Factor.Inverse();
	FactorInv.M[3][3] = 1;
	TVector<T, d> Delta = WorldSpaceX2 - WorldSpaceX1;
	return FactorInv * Delta;
}

namespace Chaos
{
	template class TPBDJointConstraintsBase2<float, 3>;
}
