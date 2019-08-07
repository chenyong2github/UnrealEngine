// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidSpringConstraintsBase.h"

using namespace Chaos;

template<class T, int d>
void TPBDRigidSpringConstraintsBase<T, d>::UpdateDistances(const TArray<TVector<T, d>>& Locations0, const TArray<TVector<T, d>>& Locations1)
{
	Distances.SetNum(Constraints.Num());
	SpringDistances.SetNum(Constraints.Num());

	const int32 NumConstraints = Constraints.Num();
	for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints; ++ConstraintIndex)
	{
		const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = Constraints[ConstraintIndex];
		const TGeometryParticleHandle<T, d>* Particle0 = Constraint[0];
		const TGeometryParticleHandle<T, d>* Particle1 = Constraint[1];

		Distances[ConstraintIndex][0] = Particle0->R().Inverse().RotateVector(Locations0[ConstraintIndex] - Particle0->X());
		Distances[ConstraintIndex][1] = Particle1->R().Inverse().RotateVector(Locations1[ConstraintIndex] - Particle1->X());
		SpringDistances[ConstraintIndex] = (Locations0[ConstraintIndex] - Locations1[ConstraintIndex]).Size();
	}
}

template<class T, int d>
TVector<T, d> TPBDRigidSpringConstraintsBase<T, d>::GetDelta(const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2, int32 ConstraintIndex) const
{
	const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = Constraints[ConstraintIndex];
	const TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint[0]->AsDynamic();
	const TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint[1]->AsDynamic();

	if (!PBDRigid0 && !PBDRigid1)
	{
		return TVector<T, d>(0);
	}

	const TVector<T, d> Difference = WorldSpaceX2 - WorldSpaceX1;

	const float Distance = Difference.Size();
	check(Distance > 1e-7);

	const TVector<T, d> Direction = Difference / Distance;
	const TVector<T, d> Delta = (Distance - SpringDistances[ConstraintIndex]) * Direction;
	const T InvM0 = (PBDRigid0) ? PBDRigid0->InvM() : (T)0;
	const T InvM1 = (PBDRigid1) ? PBDRigid1->InvM() : (T)0;
	const T CombinedMass = InvM0 + InvM1;

	return Stiffness * Delta / CombinedMass;
}

namespace Chaos
{
	template class TPBDRigidSpringConstraintsBase<float, 3>;
}
