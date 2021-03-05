// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDRigidDynamicSpringConstraints.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

using namespace Chaos;

TVec2<FGeometryParticleHandle*> FPBDRigidDynamicSpringConstraintHandle::GetConstrainedParticles() const
{
	return ConstraintContainer->GetConstrainedParticles(ConstraintIndex);
}

void FPBDRigidDynamicSpringConstraints::UpdatePositionBasedState(const FReal Dt)
{
	const int32 NumConstraints = Constraints.Num();
	for (int32 ConstraintIndex = 0; ConstraintIndex < NumConstraints; ++ConstraintIndex)
	{
		FGeometryParticleHandle* Static0 = Constraints[ConstraintIndex][0];
		FGeometryParticleHandle* Static1 = Constraints[ConstraintIndex][1];
		FPBDRigidParticleHandle* PBDRigid0 = Static0->CastToRigidParticle();
		FPBDRigidParticleHandle* PBDRigid1 = Static1->CastToRigidParticle();
		const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
		const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

		// Do not create springs between objects with no geometry
		if (!Static0->Geometry() || !Static1->Geometry())
		{
			continue;
		}

		const FRotation3& Q0 = bIsRigidDynamic0 ? PBDRigid0->Q() : Static0->R();
		const FRotation3& Q1 = bIsRigidDynamic1 ? PBDRigid1->Q() : Static1->R();
		const FVec3& P0 = bIsRigidDynamic0 ? PBDRigid0->P() : Static0->X();
		const FVec3& P1 = bIsRigidDynamic1 ? PBDRigid1->P() : Static1->X();

		// Delete constraints
		const int32 NumSprings = SpringDistances[ConstraintIndex].Num();
		for (int32 SpringIndex = NumSprings - 1; SpringIndex >= 0; --SpringIndex)
		{
			const FVec3& Distance0 = Distances[ConstraintIndex][SpringIndex][0];
			const FVec3& Distance1 = Distances[ConstraintIndex][SpringIndex][1];
			const FVec3 WorldSpaceX1 = Q0.RotateVector(Distance0) + P0;
			const FVec3 WorldSpaceX2 = Q1.RotateVector(Distance1) + P1;
			const FVec3 Difference = WorldSpaceX2 - WorldSpaceX1;
			FReal Distance = Difference.Size();
			if (Distance > CreationThreshold * 2)
			{
				Distances[ConstraintIndex].RemoveAtSwap(SpringIndex);
				SpringDistances[ConstraintIndex].RemoveAtSwap(SpringIndex);
			}
		}

		if (SpringDistances[ConstraintIndex].Num() == MaxSprings)
		{
			continue;
		}

		FRigidTransform3 Transform1(P0, Q0);
		FRigidTransform3 Transform2(P1, Q1);

		// Create constraints
		if (Static0->Geometry()->HasBoundingBox() && Static1->Geometry()->HasBoundingBox())
		{
			// Matrix multiplication is reversed intentionally to be compatible with unreal
			FAABB3 Box1 = Static0->Geometry()->BoundingBox().TransformedAABB(Transform1 * Transform2.Inverse());
			Box1.Thicken(CreationThreshold);
			FAABB3 Box2 = Static1->Geometry()->BoundingBox();
			Box2.Thicken(CreationThreshold);
			if (!Box1.Intersects(Box2))
			{
				continue;
			}
		}
		const FVec3 Midpoint = (P0 + P1) / (FReal)2.;
		FVec3 Normal1;
		const FReal Phi1 = Static0->Geometry()->PhiWithNormal(Transform1.InverseTransformPosition(Midpoint), Normal1);
		Normal1 = Transform2.TransformVector(Normal1);
		FVec3 Normal2;
		const FReal Phi2 = Static1->Geometry()->PhiWithNormal(Transform2.InverseTransformPosition(Midpoint), Normal2);
		Normal2 = Transform2.TransformVector(Normal2);
		if ((Phi1 + Phi2) > CreationThreshold)
		{
			continue;
		}
		FVec3 Location0 = Midpoint - Phi1 * Normal1;
		FVec3 Location1 = Midpoint - Phi2 * Normal2;
		TVec2<FVec3> Distance;
		Distance[0] = Q0.Inverse().RotateVector(Location0 - P0);
		Distance[1] = Q0.Inverse().RotateVector(Location1 - P1);
		Distances[ConstraintIndex].Add(MoveTemp(Distance));
		SpringDistances[ConstraintIndex].Add((Location0 - Location1).Size());
	}
}

FVec3 FPBDRigidDynamicSpringConstraints::GetDelta(const FVec3& WorldSpaceX1, const FVec3& WorldSpaceX2, const int32 ConstraintIndex, const int32 SpringIndex) const
{
	FPBDRigidParticleHandle* PBDRigid0 = Constraints[ConstraintIndex][0]->CastToRigidParticle();
	FPBDRigidParticleHandle* PBDRigid1 = Constraints[ConstraintIndex][1]->CastToRigidParticle();
	const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
	const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

	if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
	{
		return FVec3(0);
	}

	const FVec3 Difference = WorldSpaceX2 - WorldSpaceX1;
	FReal Distance = Difference.Size();
	check(Distance > 1e-7);

	const FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : (FReal)0.;
	const FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : (FReal)0.;
	const FVec3 Direction = Difference / Distance;
	const FVec3 Delta = (Distance - SpringDistances[ConstraintIndex][SpringIndex]) * Direction;
	return Stiffness * Delta / (InvM0 + InvM1);
}

void FPBDRigidDynamicSpringConstraints::ApplySingle(const FReal Dt, int32 ConstraintIndex) const
{
	FGenericParticleHandle Particle0 = Constraints[ConstraintIndex][0];
	FGenericParticleHandle Particle1 = Constraints[ConstraintIndex][1];
	const bool bIsRigidDynamic0 = Particle0->IsDynamic();
	const bool bIsRigidDynamic1 = Particle1->IsDynamic();
	check(bIsRigidDynamic0 || bIsRigidDynamic1);

	FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
	FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
	FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
	FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);

	const int32 NumSprings = SpringDistances[ConstraintIndex].Num();
	const FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, Particle0->InvI()) : FMatrix33(0);
	const FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, Particle1->InvI()) : FMatrix33(0);;
	for (int32 SpringIndex = 0; SpringIndex < NumSprings; ++SpringIndex)
	{
		const FVec3& Distance0 = Distances[ConstraintIndex][SpringIndex][0];
		const FVec3& Distance1 = Distances[ConstraintIndex][SpringIndex][1];
		const FVec3 WorldSpaceX1 = Particle0->Q().RotateVector(Distance0) + Particle0->P();
		const FVec3 WorldSpaceX2 = Particle1->Q().RotateVector(Distance1) + Particle1->P();
		const FVec3 Delta = GetDelta(WorldSpaceX1, WorldSpaceX2, ConstraintIndex, SpringIndex);

		if (bIsRigidDynamic0)
		{
			const FVec3 Radius = WorldSpaceX1 - P0;
			P0 += Particle0->InvM() * Delta;
			Q0 += FRotation3::FromElements(WorldSpaceInvI1 * FVec3::CrossProduct(Radius, Delta), 0.f) * Q0 * FReal(0.5);
			Q0.Normalize();
			FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
		}

		if (bIsRigidDynamic1)
		{
			const FVec3 Radius = WorldSpaceX2 - P1;
			P1 -= Particle1->InvM() * Delta;
			Q1 += FRotation3::FromElements(WorldSpaceInvI2 * FVec3::CrossProduct(Radius, -Delta), 0.f) * Q1 * FReal(0.5);
			Q1.Normalize();
			FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
		}
	}
}
