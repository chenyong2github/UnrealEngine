// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidSpringConstraints.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	//
	// Handle Impl
	//

	const TVector<FVec3, 2>& FPBDRigidSpringConstraintHandle::GetConstraintPositions() const
	{
		return ConstraintContainer->GetConstraintPositions(ConstraintIndex);

	}

	void FPBDRigidSpringConstraintHandle::SetConstraintPositions(const TVector<FVec3, 2>& ConstraintPositions)
	{
		ConstraintContainer->SetConstraintPositions(ConstraintIndex, ConstraintPositions);
	}
	
	TVector<typename FPBDRigidSpringConstraintHandle::FGeometryParticleHandle*, 2> FPBDRigidSpringConstraintHandle::GetConstrainedParticles() const
	{ 
		return ConstraintContainer->GetConstrainedParticles(ConstraintIndex);
	}

	FReal FPBDRigidSpringConstraintHandle::GetRestLength() const
	{
		return ConstraintContainer->GetRestLength(ConstraintIndex);
	}

	void FPBDRigidSpringConstraintHandle::SetRestLength(const FReal SpringLength)
	{
		ConstraintContainer->SetRestLength(ConstraintIndex, SpringLength);
	}

	//
	// Container Impl
	//

	FPBDRigidSpringConstraints::FPBDRigidSpringConstraints()
	{}

	FPBDRigidSpringConstraints::~FPBDRigidSpringConstraints()
	{
	}

	typename FPBDRigidSpringConstraints::FConstraintContainerHandle* FPBDRigidSpringConstraints::AddConstraint(const FConstrainedParticlePair& InConstrainedParticles, const  TVector<FVec3, 2>& InLocations, FReal Stiffness, FReal Damping, FReal RestLength)
	{
		Handles.Add(HandleAllocator.AllocHandle(this, Handles.Num()));
		int32 ConstraintIndex = Constraints.Add(InConstrainedParticles);

		SpringSettings.Emplace(FSpringSettings({ Stiffness, Damping, RestLength }));

		Distances.Add({});
		UpdateDistance(ConstraintIndex, InLocations[0], InLocations[1]);

		return Handles.Last();
	}

	void FPBDRigidSpringConstraints::RemoveConstraint(int ConstraintIndex)
	{
		FConstraintContainerHandle* ConstraintHandle = Handles[ConstraintIndex];
		if (ConstraintHandle != nullptr)
		{
			// Release the handle for the freed constraint
			HandleAllocator.FreeHandle(ConstraintHandle);
			Handles[ConstraintIndex] = nullptr;
		}

		// Swap the last constraint into the gap to keep the array packed
		Constraints.RemoveAtSwap(ConstraintIndex);
		SpringSettings.RemoveAtSwap(ConstraintIndex);
		Distances.RemoveAtSwap(ConstraintIndex);
		Handles.RemoveAtSwap(ConstraintIndex);

		// Update the handle for the constraint that was moved
		if (ConstraintIndex < Handles.Num())
		{
			FConstraintHandle* Handle = Handles[ConstraintIndex];
			SetConstraintIndex(Handle, ConstraintIndex);
		}
	}

	void FPBDRigidSpringConstraints::RemoveConstraints(const TSet<TGeometryParticleHandle<FReal, 3>*>& RemovedParticles)
	{
	}

	void FPBDRigidSpringConstraints::UpdateDistance(int32 ConstraintIndex, const FVec3& Location0, const FVec3& Location1)
	{
		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = Constraints[ConstraintIndex];
		const TGeometryParticleHandle<FReal, 3>* Particle0 = Constraint[0];
		const TGeometryParticleHandle<FReal, 3>* Particle1 = Constraint[1];

		Distances[ConstraintIndex][0] = Particle0->R().Inverse().RotateVector(Location0 - Particle0->X());
		Distances[ConstraintIndex][1] = Particle1->R().Inverse().RotateVector(Location1 - Particle1->X());
	}

	FVec3 FPBDRigidSpringConstraints::GetDelta(int32 ConstraintIndex, const FVec3& WorldSpaceX1, const FVec3& WorldSpaceX2) const
	{
		const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& Constraint = Constraints[ConstraintIndex];
		const TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Constraint[0]->CastToRigidParticle();
		const TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Constraint[1]->CastToRigidParticle();
		const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
		const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

		if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
		{
			return FVec3(0);
		}

		const FVec3 Difference = WorldSpaceX2 - WorldSpaceX1;

		const float Distance = Difference.Size();
		check(Distance > 1e-7);

		const FVec3 Direction = Difference / Distance;
		const FVec3 Delta = (Distance - SpringSettings[ConstraintIndex].RestLength) * Direction;
		const FReal InvM0 = (bIsRigidDynamic0) ? PBDRigid0->InvM() : (FReal)0;
		const FReal InvM1 = (bIsRigidDynamic1) ? PBDRigid1->InvM() : (FReal)0;
		const FReal CombinedMass = InvM0 + InvM1;

		return SpringSettings[ConstraintIndex].Stiffness * Delta / CombinedMass;
	}

	void FPBDRigidSpringConstraints::Apply(const FReal Dt, const int32 It, const int32 NumIts)
	{
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			ApplySingle(Dt, ConstraintIndex);
		}
	}

	void FPBDRigidSpringConstraints::Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		for (FConstraintContainerHandle* ConstraintHandle : InConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
		}
	}

	void FPBDRigidSpringConstraints::ApplySingle(const FReal Dt, int32 ConstraintIndex) const
	{
		const FConstrainedParticlePair& Constraint = Constraints[ConstraintIndex];

		TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Constraint[0]->CastToRigidParticle();
		TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Constraint[1]->CastToRigidParticle();
		const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
		const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

		check((bIsRigidDynamic0 && bIsRigidDynamic1) || (!bIsRigidDynamic0 && bIsRigidDynamic1) || (bIsRigidDynamic0 && bIsRigidDynamic1));

		// @todo(ccaulfield): see if we can eliminate the need for all these ifs
		const TRotation<FReal, 3> & Q0 = bIsRigidDynamic0 ? PBDRigid0->Q() : Constraint[0]->R();
		const TRotation<FReal, 3> & Q1 = bIsRigidDynamic1 ? PBDRigid1->Q() : Constraint[1]->R();
		const FVec3 & P0 = bIsRigidDynamic0 ? PBDRigid0->P() : Constraint[0]->X();
		const FVec3 & P1 = bIsRigidDynamic1 ? PBDRigid1->P() : Constraint[1]->X();

		const FVec3 WorldSpaceX1 = Q0.RotateVector(Distances[ConstraintIndex][0]) + P0;
		const FVec3 WorldSpaceX2 = Q1.RotateVector(Distances[ConstraintIndex][1]) + P1;
		const FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) : FMatrix33(0);
		const FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) : FMatrix33(0);
		const FVec3 Delta = GetDelta(ConstraintIndex, WorldSpaceX1, WorldSpaceX2);

		if (bIsRigidDynamic0)
		{
			const FVec3 Radius = WorldSpaceX1 - PBDRigid0->P();
			PBDRigid0->P() += PBDRigid0->InvM() * Delta;
			PBDRigid0->Q() += TRotation<FReal, 3>::FromElements(WorldSpaceInvI1 * FVec3::CrossProduct(Radius, Delta), 0.f) * PBDRigid0->Q() * FReal(0.5);
			PBDRigid0->Q().Normalize();
		}

		if (bIsRigidDynamic1)
		{
			const FVec3 Radius = WorldSpaceX2 - PBDRigid1->P();
			PBDRigid1->P() -= PBDRigid1->InvM() * Delta;
			PBDRigid1->Q() += TRotation<FReal, 3>::FromElements(WorldSpaceInvI2 * FVec3::CrossProduct(Radius, -Delta), 0.f) * PBDRigid1->Q() * FReal(0.5);
			PBDRigid1->Q().Normalize();
		}
	}
}
