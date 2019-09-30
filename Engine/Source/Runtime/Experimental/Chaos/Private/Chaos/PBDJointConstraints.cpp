// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Utilities.h"

using namespace Chaos;

//
// Constraint Handle
//

template<class T, int d>
const TVector<TVector<T, 3>, 2>& TPBDJointConstraintHandle<T, d>::GetConstraintPositions() const
{
	return ConstraintContainer->GetConstraintPositions(ConstraintIndex);
}

template<class T, int d>
void TPBDJointConstraintHandle<T, d>::SetConstraintPositions(const TVector<TVector<T, 3>, 2>& ConstraintPositions)
{
	ConstraintContainer->SetConstraintPositions(ConstraintIndex, ConstraintPositions);
}



//
// Constraint Container
//

template<class T, int d>
void TPBDJointConstraints<T, d>::UpdateDistanceInternal(const TVector<T, d>& InLocation, const int32 InConstraintIndex)
{
	const TGeometryParticleHandle<T, d>* Particle0 = Constraints[InConstraintIndex][0];
	const TGeometryParticleHandle<T, d>* Particle1 = Constraints[InConstraintIndex][1];
	Distances[InConstraintIndex][0] = Particle0->R().Inverse().RotateVector(InLocation - Particle0->X());
	Distances[InConstraintIndex][1] = Particle1->R().Inverse().RotateVector(InLocation - Particle1->X());
}

template<class T, int d>
void TPBDJointConstraints<T, d>::UpdateDistance(const TVector<T, d>& InLocation, const int32 InConstraintIndex)
{
	Distances.SetNum(Constraints.Num());
	UpdateDistanceInternal(InLocation, InConstraintIndex);
}

template<class T, int d>
void TPBDJointConstraints<T, d>::UpdateDistances(const TArray<TVector<T, d>>& InLocations)
{
	const int32 NumConstraints = Constraints.Num();
	Distances.SetNum(NumConstraints);
	for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints; ++ConstraintIndex)
	{
		UpdateDistanceInternal(InLocations[ConstraintIndex], ConstraintIndex);
	}
}

template<class T, int d>
TVector<T, d> TPBDJointConstraints<T, d>::GetDeltaDynamicDynamic(const TVector<T, d>& P0, const TVector<T, d>& P1, const TVector<T, d>& C0, const TVector<T, d>& C1, const PMatrix<T, d, d>& InvI0, const PMatrix<T, d, d>& InvI1, const T InvM0, const T InvM1)
{
	PMatrix<T, d, d> Factor = Utilities::ComputeJointFactorMatrix(C0 - P0, InvI0, InvM0) + Utilities::ComputeJointFactorMatrix(C1 - P1, InvI1, InvM1);
	PMatrix<T, d, d> FactorInv = Factor.Inverse();
	FactorInv.M[3][3] = 1;
	TVector<T, d> Delta = C1 - C0;
	return FactorInv * Delta;
}

template<class T, int d>
void TPBDJointConstraints<T, d>::ApplySingle(const T Dt, const int32 ConstraintIndex)
{
	// @todo(ccaulfield): bApplyProjection should be an option. Either per-constraint or per-container...
	const bool bApplyProjection = true;

	const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = Constraints[ConstraintIndex];
	if (Constraint[0]->AsDynamic() && Constraint[1]->AsDynamic())
	{
		ApplyDynamicDynamic(Dt, ConstraintIndex, 0, 1, bApplyProjection);
	}
	else if (Constraint[0]->AsDynamic())
	{
		ApplyDynamicStatic(Dt, ConstraintIndex, 0, 1, bApplyProjection);
	}
	else
	{
		ApplyDynamicStatic(Dt, ConstraintIndex, 1, 0, bApplyProjection);
	}
}

template<class T, int d>
void TPBDJointConstraints<T, d>::ApplyDynamicDynamic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 PBDRigid1Index, const bool bApplyProjection)
{
	check((PBDRigid0Index == 0) || (PBDRigid0Index == 1));
	check((PBDRigid1Index == 0) || (PBDRigid1Index == 1));
	check(PBDRigid0Index != PBDRigid1Index);

	TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraints[ConstraintIndex][PBDRigid0Index]->AsDynamic();
	TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraints[ConstraintIndex][PBDRigid1Index]->AsDynamic();
	check(PBDRigid0 && PBDRigid1 && (PBDRigid0->Island() == PBDRigid1->Island()));

	TRotation<T, d>& Q0 = PBDRigid0->Q();
	TVector<T, d>& P0 = PBDRigid0->P();
	TRotation<T, d>& Q1 = PBDRigid1->Q();
	TVector<T, d>& P1 = PBDRigid1->P();

	// Calculate world-space constraint positions
	const TVector<T, 3>& Distance0 = Distances[ConstraintIndex][PBDRigid0Index];
	const TVector<T, 3>& Distance1 = Distances[ConstraintIndex][PBDRigid1Index];
	const TVector<T, d> C0 = Q0.RotateVector(Distance0) + P0;
	const TVector<T, d> C1 = Q1.RotateVector(Distance1) + P1;

	// Calculate world-space mass
	const PMatrix<T, d, d> InvI0 = (Q0 * FMatrix::Identity) * PBDRigid0->InvI() * (Q0 * FMatrix::Identity).GetTransposed();
	const PMatrix<T, d, d> InvI1 = (Q1 * FMatrix::Identity) * PBDRigid1->InvI() * (Q1 * FMatrix::Identity).GetTransposed();
	const float InvM0 = PBDRigid0->InvM();
	const float InvM1 = PBDRigid1->InvM();

	// Calculate mass-weighted correction
	const TVector<T, d> Delta = GetDeltaDynamicDynamic(P0, P1, C0, C1, InvI0, InvI1, InvM0, InvM1);

	// Apply corrections
	P0 += InvM0 * Delta;
	Q0 += TRotation<T, d>(InvI0 * TVector<T, d>::CrossProduct(C0 - P0, Delta), 0.f) * Q0 * T(0.5);
	Q0.Normalize();

	P1 -= InvM1 * Delta;
	Q1 += TRotation<T, d>(InvI1 * TVector<T, d>::CrossProduct(C1 - P1, -Delta), 0.f) * Q1 * T(0.5);
	Q1.Normalize();

	// Correct any remaining error by translating
	if (bApplyProjection)
	{
		const TVector<T, d> C0Proj = Q0.RotateVector(Distance0) + P0;
		const TVector<T, d> C1Proj = Q1.RotateVector(Distance1) + P1;
		const TVector<T, d> DeltaProj = (C1Proj - C0Proj) / (InvM0 + InvM1);

		P0 += InvM0 * DeltaProj;
		P1 -= InvM1 * DeltaProj;
	}
}

template<class T, int d>
TVector<T, d> TPBDJointConstraints<T, d>::GetDeltaDynamicKinematic(const TVector<T, d>& P0, const TVector<T, d>& C0, const TVector<T, d>& C1, const PMatrix<T, d, d>& InvI0, const T InvM0)
{
	PMatrix<T, d, d> Factor = Utilities::ComputeJointFactorMatrix(C0 - P0, InvI0, InvM0);
	PMatrix<T, d, d> FactorInv = Factor.Inverse();
	FactorInv.M[3][3] = 1;
	TVector<T, d> Delta = C1 - C0;
	return FactorInv * Delta;
}

template<class T, int d>
void TPBDJointConstraints<T, d>::ApplyDynamicStatic(const T Dt, const int32 ConstraintIndex, const int32 PBDRigid0Index, const int32 Static1Index, const bool bApplyProjection)
{
	check((PBDRigid0Index == 0) || (PBDRigid0Index == 1));
	check((Static1Index == 0) || (Static1Index == 1));
	check(PBDRigid0Index != Static1Index);

	TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraints[ConstraintIndex][PBDRigid0Index]->AsDynamic();
	TGeometryParticleHandle<T, d>* Static1 = Constraints[ConstraintIndex][Static1Index];
	check(PBDRigid0 && Static1 && !Static1->AsDynamic());

	TRotation<T, d>& Q0 = PBDRigid0->Q();
	TVector<T, d>& P0 = PBDRigid0->P();
	const TRotation<T, d>& Q1 = Static1->R();
	const TVector<T, d>& P1 = Static1->X();

	// Calculate world-space constraint positions
	const TVector<T, 3>& Distance0 = Distances[ConstraintIndex][PBDRigid0Index];
	const TVector<T, 3>& Distance1 = Distances[ConstraintIndex][Static1Index];
	const TVector<T, d> C0 = Q0.RotateVector(Distance0) + P0;
	const TVector<T, d> C1 = Q1.RotateVector(Distance1) + P1;

	// Calculate world-space mass
	const PMatrix<T, d, d> InvI0 = (Q0 * FMatrix::Identity) * PBDRigid0->InvI() * (Q0 * FMatrix::Identity).GetTransposed();
	const float InvM0 = PBDRigid0->InvM();

	// Calculate mass-weighted correction
	const TVector<T, d> Delta = GetDeltaDynamicKinematic(P0, C0, C1, InvI0, InvM0);

	// Apply correction
	P0 += InvM0 * Delta;
	Q0 += TRotation<T, d>(InvI0 * TVector<T, d>::CrossProduct(C0 - P0, Delta), 0.f) * Q0 * T(0.5);
	Q0.Normalize();

	// Correct any remaining error by translating
	if (bApplyProjection)
	{
		const TVector<T, d> C0Proj = Q0.RotateVector(Distance0) + P0;
		const TVector<T, d> C1Proj = Q1.RotateVector(Distance1) + P1;
		const TVector<T, d> DeltaProj = (C1Proj - C0Proj);

		P0 += DeltaProj;
	}
}


namespace Chaos
{
	template class Chaos::TPBDJointConstraintHandle<float, 3>;
	template class Chaos::TPBDJointConstraints<float, 3>;
}
