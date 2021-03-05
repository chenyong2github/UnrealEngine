// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidSpringConstraints.h"
#include "Chaos/Particle/ParticleUtilities.h"
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

		const FReal Distance = Difference.Size();
		check(Distance > 1e-7);

		const FVec3 Direction = Difference / Distance;
		const FVec3 Delta = (Distance - SpringSettings[ConstraintIndex].RestLength) * Direction;
		const FReal InvM0 = (bIsRigidDynamic0) ? PBDRigid0->InvM() : (FReal)0;
		const FReal InvM1 = (bIsRigidDynamic1) ? PBDRigid1->InvM() : (FReal)0;
		const FReal CombinedMass = InvM0 + InvM1;

		return SpringSettings[ConstraintIndex].Stiffness * Delta / CombinedMass;
	}

	bool FPBDRigidSpringConstraints::Apply(const FReal Dt, const int32 It, const int32 NumIts)
	{
		for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints(); ++ConstraintIndex)
		{
			ApplySingle(Dt, ConstraintIndex);
		}
		return false;
	}

	bool FPBDRigidSpringConstraints::Apply(const FReal Dt, const TArray<FConstraintContainerHandle*>& InConstraintHandles, const int32 It, const int32 NumIts)
	{
		for (FConstraintContainerHandle* ConstraintHandle : InConstraintHandles)
		{
			ApplySingle(Dt, ConstraintHandle->GetConstraintIndex());
		}
		return false;
	}

	void FPBDRigidSpringConstraints::ApplySingle(const FReal Dt, int32 ConstraintIndex) const
	{
		const FConstrainedParticlePair& Constraint = Constraints[ConstraintIndex];

		FGenericParticleHandle Particle0 = Constraints[ConstraintIndex][0];
		FGenericParticleHandle Particle1 = Constraints[ConstraintIndex][1];
		const bool bIsRigidDynamic0 = Particle0->IsDynamic();
		const bool bIsRigidDynamic1 = Particle1->IsDynamic();

		check((bIsRigidDynamic0 && bIsRigidDynamic1) || (!bIsRigidDynamic0 && bIsRigidDynamic1) || (bIsRigidDynamic0 && bIsRigidDynamic1));

		const FVec3 WorldSpaceX1 = Particle0->Q().RotateVector(Distances[ConstraintIndex][0]) + Particle0->P();
		const FVec3 WorldSpaceX2 = Particle1->Q().RotateVector(Distances[ConstraintIndex][1]) + Particle1->P();
		const FVec3 Delta = GetDelta(ConstraintIndex, WorldSpaceX1, WorldSpaceX2);

		if (bIsRigidDynamic0)
		{
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			const FMatrix33 WorldSpaceInvI1 = Utilities::ComputeWorldSpaceInertia(Q0, Particle0->InvI());
			const FVec3 Radius = WorldSpaceX1 - P0;
			P0 += Particle0->InvM() * Delta;
			Q0 += TRotation<FReal, 3>::FromElements(WorldSpaceInvI1 * FVec3::CrossProduct(Radius, Delta), 0.f) * Q0 * FReal(0.5);
			Q0.Normalize();
			FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
		}

		if (bIsRigidDynamic1)
		{
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			const FMatrix33 WorldSpaceInvI2 = Utilities::ComputeWorldSpaceInertia(Q1, Particle1->InvI());
			const FVec3 Radius = WorldSpaceX2 - P1;
			P1 -= Particle1->InvM() * Delta;
			Q1 += TRotation<FReal, 3>::FromElements(WorldSpaceInvI2 * FVec3::CrossProduct(Radius, -Delta), 0.f) * Q1 * FReal(0.5);
			Q1.Normalize();
			FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
		}
	}
}
