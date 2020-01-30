// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidSpringConstraints.h"
#include "Chaos/Utilities.h"

namespace Chaos
{

	template<class T, int d>
	void TPBDRigidSpringConstraints<T, d>::UpdateDistance(int32 ConstraintIndex, const TVector<T, d>& Location0, const TVector<T, d>& Location1)
	{
		const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = Constraints[ConstraintIndex];
		const TGeometryParticleHandle<T, d>* Particle0 = Constraint[0];
		const TGeometryParticleHandle<T, d>* Particle1 = Constraint[1];

		Distances[ConstraintIndex][0] = Particle0->R().Inverse().RotateVector(Location0 - Particle0->X());
		Distances[ConstraintIndex][1] = Particle1->R().Inverse().RotateVector(Location1 - Particle1->X());
		SpringDistances[ConstraintIndex] = (Location0 - Location1).Size();
	}

	template<class T, int d>
	TVector<T, d> TPBDRigidSpringConstraints<T, d>::GetDelta(int32 ConstraintIndex, const TVector<T, d>& WorldSpaceX1, const TVector<T, d>& WorldSpaceX2) const
	{
		const TVector<TGeometryParticleHandle<T, d>*, 2>& Constraint = Constraints[ConstraintIndex];
		const TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint[0]->CastToRigidParticle();
		const TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint[1]->CastToRigidParticle();
		const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
		const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

		if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
		{
			return TVector<T, d>(0);
		}

		const TVector<T, d> Difference = WorldSpaceX2 - WorldSpaceX1;

		const float Distance = Difference.Size();
		check(Distance > 1e-7);

		const TVector<T, d> Direction = Difference / Distance;
		const TVector<T, d> Delta = (Distance - SpringDistances[ConstraintIndex]) * Direction;
		const T InvM0 = (bIsRigidDynamic0) ? PBDRigid0->InvM() : (T)0;
		const T InvM1 = (bIsRigidDynamic1) ? PBDRigid1->InvM() : (T)0;
		const T CombinedMass = InvM0 + InvM1;

		return Stiffness * Delta / CombinedMass;
	}

	template<class T, int d>
	void TPBDRigidSpringConstraints<T, d>::Apply(const T Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		for (FConstraintContainerHandle* ConstraintHandle : InConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
		}
	}


	template<class T, int d>
	void TPBDRigidSpringConstraints<T, d>::ApplySingle(const T Dt, int32 ConstraintIndex) const
	{
		const FConstrainedParticlePair& Constraint = Constraints[ConstraintIndex];

		TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint[0]->CastToRigidParticle();
		TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint[1]->CastToRigidParticle();
		const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
		const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

		check((bIsRigidDynamic0 && bIsRigidDynamic1 && PBDRigid0->Island() == PBDRigid1->Island()) || (!bIsRigidDynamic0 && bIsRigidDynamic1) || (bIsRigidDynamic0 && bIsRigidDynamic1));

		// @todo(ccaulfield): see if we can eliminate the need for all these ifs
		const TRotation<T, d> & Q0 = bIsRigidDynamic0 ? PBDRigid0->Q() : Constraint[0]->R();
		const TRotation<T, d> & Q1 = bIsRigidDynamic1 ? PBDRigid1->Q() : Constraint[1]->R();
		const TVector<T, d> & P0 = bIsRigidDynamic0 ? PBDRigid0->P() : Constraint[0]->X();
		const TVector<T, d> & P1 = bIsRigidDynamic1 ? PBDRigid1->P() : Constraint[1]->X();

		const TVector<T, d> WorldSpaceX1 = Q0.RotateVector(Distances[ConstraintIndex][0]) + P0;
		const TVector<T, d> WorldSpaceX2 = Q1.RotateVector(Distances[ConstraintIndex][1]) + P1;
		const PMatrix<T, d, d> WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) : PMatrix<T, d, d>(0);
		const PMatrix<T, d, d> WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) : PMatrix<T, d, d>(0);
		const TVector<T, d> Delta = GetDelta(ConstraintIndex, WorldSpaceX1, WorldSpaceX2);

		if (bIsRigidDynamic0)
		{
			const TVector<T, d> Radius = WorldSpaceX1 - PBDRigid0->P();
			PBDRigid0->P() += PBDRigid0->InvM() * Delta;
			PBDRigid0->Q() += TRotation<T, d>::FromElements(WorldSpaceInvI1 * TVector<T, d>::CrossProduct(Radius, Delta), 0.f) * PBDRigid0->Q() * T(0.5);
			PBDRigid0->Q().Normalize();
		}

		if (bIsRigidDynamic1)
		{
			const TVector<T, d> Radius = WorldSpaceX2 - PBDRigid1->P();
			PBDRigid1->P() -= PBDRigid1->InvM() * Delta;
			PBDRigid1->Q() += TRotation<T, d>::FromElements(WorldSpaceInvI2 * TVector<T, d>::CrossProduct(Radius, -Delta), 0.f) * PBDRigid1->Q() * T(0.5);
			PBDRigid1->Q().Normalize();
		}
	}

	template class TPBDRigidSpringConstraints<float, 3>;
}
