// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ContactModification.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
namespace Chaos
{
	void FContactPairModifier::Disable()
	{
		Modifier->DisableConstraint(*Constraint);
	}

	void FContactPairModifier::Enable()
	{
		Modifier->EnableConstraint(*Constraint);
	}

	int32 FContactPairModifier::GetNumContacts() const
	{
		return Constraint->GetManifoldPoints().Num();
	}

	int32 FContactPairModifier::GetDeepestContactIndex() const
	{
		TArrayView<const FManifoldPoint> ManifoldPoints = Constraint->GetManifoldPoints();

		// We could use GetSeparation() here, but Phi avoids some computation.

		FReal DeepestSeparation = ManifoldPoints[0].ContactPoint.Phi;
		int32 DeepestIdx = 0;

		for (int32 Idx = 1; Idx < ManifoldPoints.Num(); ++Idx)
		{
			const FReal Separation = ManifoldPoints[Idx].ContactPoint.Phi;
			if (Separation < DeepestSeparation)
			{
				DeepestSeparation = Separation;
				DeepestIdx = Idx;
			}
		}

		return DeepestIdx;
	}


	const FImplicitObject* FContactPairModifier::GetContactGeometry(int32 ParticleIdx)
	{
		return Constraint->Manifold.Implicit[ParticleIdx];
	}

	FRigidTransform3 FContactPairModifier::GetShapeToWorld(int32 ParticleIdx) const
	{
		TVec2<FGeometryParticleHandle*> Particles = GetParticlePair();
		FGeometryParticleHandle*& Particle = Particles[ParticleIdx];
		
		if (FPBDRigidParticleHandle* Rigid = Particle->CastToRigidParticle())
		{
			// Use PQ for rigids.
			return Constraint->ImplicitTransform[ParticleIdx] * FParticleUtilitiesPQ::GetActorWorldTransform(Rigid);
		}

		return Constraint->ImplicitTransform[ParticleIdx] * FParticleUtilitiesXR::GetActorWorldTransform(Particle);
	}

	FReal FContactPairModifier::GetSeparation(int32 ContactPointIdx) const
	{
		// Compute separation with distance between contact points.

		FVec3 WorldPos0, WorldPos1;
		GetWorldContactLocations(ContactPointIdx, WorldPos0, WorldPos1);
		const FVec3 Normal = GetWorldNormal(ContactPointIdx);
		return FVec3::DotProduct(Normal, (WorldPos0 - WorldPos1));
	}

	void FContactPairModifier::ModifySeparation(FReal Separation, int32 ContactPointIdx)
	{	
		FVec3 WorldPos0, WorldPos1;
		GetWorldContactLocations(ContactPointIdx, WorldPos0, WorldPos1);

		const FVec3 Normal = GetWorldNormal(ContactPointIdx);
		const FReal CurrentSeparation = FVec3::DotProduct(Normal, (WorldPos0 - WorldPos1));
		const FReal DeltaSeparation = Separation - CurrentSeparation;
		
		// Adjust contact locations to match desired separation
		WorldPos0 += 0.5f * DeltaSeparation * Normal;
		WorldPos1 -= 0.5f * DeltaSeparation * Normal;
		ModifyWorldContactLocations(WorldPos0, WorldPos1, ContactPointIdx);

		Modifier->MarkConstraintForManifoldUpdate(*Constraint);
	}

	FVec3 FContactPairModifier::GetWorldNormal(int32 ContactPointIdx) const
	{
		return Constraint->GetManifoldPoints()[ContactPointIdx].ContactPoint.Normal;
	}

	void FContactPairModifier::ModifyWorldNormal(const FVec3& Normal, int32 ContactPointIdx)
	{
		TArrayView<FManifoldPoint> ManifoldPoints = Constraint->GetManifoldPoints();
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ContactPointIdx];

		ManifoldPoint.ContactPoint.Normal = Normal;
		ManifoldPoint.ContactPoint.Phi = FVec3::DotProduct(ManifoldPoint.WorldContactPoints[0] - ManifoldPoint.WorldContactPoints[1], ManifoldPoint.ContactPoint.Normal);

		Modifier->MarkConstraintForManifoldUpdate(*Constraint);
	}

	void FContactPairModifier::GetWorldContactLocations(int32 ContactPointIdx, FVec3& OutLocation0, FVec3& OutLocation1) const
	{
		TArrayView<FManifoldPoint> ManifoldPoints = Constraint->GetManifoldPoints();
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ContactPointIdx];
		OutLocation0 = ManifoldPoint.WorldContactPoints[0];
		OutLocation1 = ManifoldPoint.WorldContactPoints[1];
	}

	FVec3 FContactPairModifier::GetWorldContactLocation(int32 ContactPointIdx) const
	{
		FVec3 WorldPos0, WorldPos1;
		GetWorldContactLocations(ContactPointIdx, WorldPos0, WorldPos1);
		return (WorldPos0 + WorldPos1) * FReal(0.5);
	}

	void FContactPairModifier::ModifyWorldContactLocations(const FVec3& Location0, const FVec3& Location1, int32 ContactPointIdx)
	{
		TArrayView<FManifoldPoint> ManifoldPoints = Constraint->GetManifoldPoints();
		FManifoldPoint& ManifoldPoint = ManifoldPoints[ContactPointIdx];

		ManifoldPoint.ContactPoint.ShapeContactPoints[0] = Constraint->GetShapeWorldTransform0().InverseTransformPositionNoScale(Location0);
		ManifoldPoint.ContactPoint.ShapeContactPoints[1] = Constraint->GetShapeWorldTransform1().InverseTransformPositionNoScale(Location1);
		// @todo(chaos): Overwriting ShapeAnchorPoints disables static friction for this tick - we might want to do something better here
		ManifoldPoint.ShapeAnchorPoints[0] = ManifoldPoint.ContactPoint.ShapeContactPoints[0];
		ManifoldPoint.ShapeAnchorPoints[1] = ManifoldPoint.ContactPoint.ShapeContactPoints[1];
		ManifoldPoint.WorldContactPoints[0] = Location0;
		ManifoldPoint.WorldContactPoints[1] = Location1;
		ManifoldPoint.ContactPoint.Location = 0.5 * (Location0 + Location1);

		Modifier->MarkConstraintForManifoldUpdate(*Constraint);
	}

	FReal FContactPairModifier::GetRestitution() const
	{
		return Constraint->GetManifold().Restitution;
	}

	void FContactPairModifier::ModifyRestitution(FReal Restitution)
	{
		Constraint->Manifold.Restitution = Restitution;
	}

	FReal FContactPairModifier::GetRestitutionThreshold() const
	{
		return Constraint->Manifold.RestitutionThreshold;
	}

	void FContactPairModifier::ModifyRestitutionThreshold(FReal Threshold)
	{
		Constraint->Manifold.RestitutionThreshold = Threshold;
	}

	FReal FContactPairModifier::GetDynamicFriction() const
	{
		return Constraint->Manifold.Friction;
	}

	void FContactPairModifier::ModifyDynamicFriction(FReal DynamicFriction)
	{
		Constraint->Manifold.Friction = DynamicFriction;
	}

	FReal FContactPairModifier::GetStaticFriction() const
	{
		return Constraint->Manifold.AngularFriction;
	}

	void FContactPairModifier::ModifyStaticFriction(FReal StaticFriction)
	{
		Constraint->Manifold.AngularFriction = StaticFriction;
	}

	FVec3 FContactPairModifier::GetParticleVelocity(int32 ParticleIdx) const
	{
		const FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];
		const FKinematicGeometryParticleHandle* KinematicHandle = Particle->CastToKinematicParticle();
		if (!ensure(KinematicHandle))
		{
			// Cannot get velocity from static
			return FVec3(0);
		}

		return KinematicHandle->V();
	}

	void FContactPairModifier::ModifyParticleVelocity(FVec3 Velocity, int32 ParticleIdx)
	{
		FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];
		FKinematicGeometryParticleHandle* KinematicHandle = Particle->CastToKinematicParticle();
		if (!ensure(KinematicHandle))
		{
			// Cannot modify velocity on static
			return;
		}

		KinematicHandle->SetV(Velocity);

		// Simulated object must update implicit velocity
		if (FPBDRigidParticleHandle* RigidHandle = Particle->CastToRigidParticle())
		{
			EObjectStateType State(RigidHandle->ObjectState());
			if (State == EObjectStateType::Dynamic || State == EObjectStateType::Sleeping)
			{
				RigidHandle->SetX(RigidHandle->P() - Velocity * Modifier->Dt);
			}
		}
	}


	FVec3 FContactPairModifier::GetParticleAngularVelocity(int32 ParticleIdx) const
	{
		const FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];
		const FKinematicGeometryParticleHandle* KinematicHandle = Particle->CastToKinematicParticle();
		if (!ensure(KinematicHandle))
		{
			// Cannot get velocity from static
			return FVec3(0);
		}

		return KinematicHandle->W();
	}

	void FContactPairModifier::ModifyParticleAngularVelocity(FVec3 AngularVelocity, int32 ParticleIdx)
	{
		FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];
		FKinematicGeometryParticleHandle* KinematicHandle = Particle->CastToKinematicParticle();
		if (!ensure(KinematicHandle))
		{
			return;
		}

		KinematicHandle->SetW(AngularVelocity);

		// Simulated object must update implicit velocity
		if (FPBDRigidParticleHandle* RigidHandle = Particle->CastToRigidParticle())
		{
			EObjectStateType State(RigidHandle->ObjectState());
			if (State == EObjectStateType::Dynamic || State == EObjectStateType::Sleeping)
			{
				RigidHandle->SetR(FRotation3::IntegrateRotationWithAngularVelocity(RigidHandle->Q(), -RigidHandle->W(), Modifier->Dt));
			}
		}
	}

	FVec3 FContactPairModifier::GetParticlePosition(int32 ParticleIdx) const
	{
		const FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];
		const FPBDRigidParticleHandle* RigidHandle = Particle->CastToRigidParticle();

		if (RigidHandle)
		{
			return RigidHandle->P();
		}

		return Particle->X();
	}

	void FContactPairModifier::UpdateConstraintShapeTransforms()
	{
		const FRigidTransform3 ShapeWorldTransform0 = Constraint->GetShapeRelativeTransform0() * FParticleUtilitiesPQ::GetActorWorldTransform(FConstGenericParticleHandle(Constraint->GetParticle0()));
		const FRigidTransform3 ShapeWorldTransform1 = Constraint->GetShapeRelativeTransform1() * FParticleUtilitiesPQ::GetActorWorldTransform(FConstGenericParticleHandle(Constraint->GetParticle1()));
		Constraint->SetShapeWorldTransforms(ShapeWorldTransform0, ShapeWorldTransform1);
	}

	void FContactPairModifier::ModifyParticlePosition(FVec3 Position, bool bMaintainVelocity, int32 ParticleIdx)
	{
		FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];

		Modifier->MarkConstraintForManifoldUpdate(*Constraint);

		if (FPBDRigidParticleHandle* RigidHandle = Particle->CastToRigidParticle())
		{
			EObjectStateType State(RigidHandle->ObjectState());
			if (State == EObjectStateType::Dynamic || State == EObjectStateType::Sleeping)
			{
				RigidHandle->SetP(Position);

				if (bMaintainVelocity)
				{
					RigidHandle->SetX(RigidHandle->P() - RigidHandle->V() * Modifier->Dt);
				}
				else if(Modifier->Dt > 0.0f)
				{
					// Update V to new implicit velocity
					RigidHandle->SetV((RigidHandle->P() - RigidHandle->X()) / Modifier->Dt);
				}
				UpdateConstraintShapeTransforms();
				return;
			}
			else
			{
				// Kinematic must keep P/X in sync
				RigidHandle->SetX(Position);
				RigidHandle->SetP(Position);
				UpdateConstraintShapeTransforms();
				return;
			}
		}
		
		// Handle kinematic that is not PBDRigid type
		FKinematicGeometryParticleHandle* KinematicHandle = Particle->CastToKinematicParticle();
		if (KinematicHandle)
		{
			KinematicHandle->SetX(Position);
			UpdateConstraintShapeTransforms();
			return;
		}

		ensure(false); // Called on static?
	}


	FRotation3 FContactPairModifier::GetParticleRotation(int32 ParticleIdx) const
	{
		const FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];
		const FPBDRigidParticleHandle* RigidHandle = Particle->CastToRigidParticle();


		// We give predicted position for simulated objects
		if (RigidHandle)
		{
			return RigidHandle->Q();
		}

		return Particle->R();
	}

	void FContactPairModifier::ModifyParticleRotation(FRotation3 Rotation, bool bMaintainVelocity, int32 ParticleIdx)
	{
		FGeometryParticleHandle* Particle = Constraint->Particle[ParticleIdx];

		Modifier->MarkConstraintForManifoldUpdate(*Constraint);

		if (FPBDRigidParticleHandle* RigidHandle = Particle->CastToRigidParticle())
		{
			EObjectStateType State(RigidHandle->ObjectState());
			if (State == EObjectStateType::Dynamic || State == EObjectStateType::Sleeping)
			{
				RigidHandle->SetQ(Rotation);

				if (bMaintainVelocity)
				{
					RigidHandle->SetR(FRotation3::IntegrateRotationWithAngularVelocity(RigidHandle->Q(), -RigidHandle->W(), Modifier->Dt));
				}
				else if (Modifier->Dt > 0.0f)
				{
					// Update W to new implicit velocity
					RigidHandle->SetW(FRotation3::CalculateAngularVelocity(RigidHandle->R(), RigidHandle->Q(), Modifier->Dt));
				}
				UpdateConstraintShapeTransforms();
				return;
			}
			else
			{
				// Kinematic must keep Q/R in sync
				RigidHandle->SetR(Rotation);
				RigidHandle->SetQ(Rotation);
				UpdateConstraintShapeTransforms();
				return;
			}
		}
		// Handle kinematic that is not PBDRigid type
		FKinematicGeometryParticleHandle* KinematicHandle = Particle->CastToKinematicParticle();
		if (KinematicHandle)
		{
			KinematicHandle->SetR(Rotation);
			UpdateConstraintShapeTransforms();
			return;
		}

		ensure(false); // Called on static?
	}


	FReal FContactPairModifier::GetInvInertiaScale(int32 ParticleIdx) const
	{
		return ParticleIdx == 0 ? Constraint->Manifold.InvInertiaScale0 : Constraint->Manifold.InvInertiaScale1;
	}

	void FContactPairModifier::ModifyInvInertiaScale(FReal InInvInertiaScale, int32 ParticleIdx)
	{
		FReal& InvInertiaScale = ParticleIdx == 0 ? Constraint->Manifold.InvInertiaScale0 : Constraint->Manifold.InvInertiaScale1;
		InvInertiaScale = InInvInertiaScale;
	}

	FReal FContactPairModifier::GetInvMassScale(int32 ParticleIdx) const
	{
		return ParticleIdx == 0 ? Constraint->Manifold.InvMassScale0 : Constraint->Manifold.InvMassScale1;
	}

	void FContactPairModifier::ModifyInvMassScale(FReal InInvMassScale, int32 ParticleIdx)
	{
		FReal& InvMassScale = ParticleIdx == 0 ? Constraint->Manifold.InvMassScale0 : Constraint->Manifold.InvMassScale1;
		InvMassScale = InInvMassScale;
	}

	TVec2<FGeometryParticleHandle*> FContactPairModifier::GetParticlePair() const
	{
		return { Constraint->Particle[0], Constraint->Particle[1] };
	}

	void FContactPairModifierIterator::SeekValidContact()
	{
		// Not valid to call from end.
		if (!ensure(IsValid()))
		{
			return;
		}

		TArrayView<FPBDCollisionConstraint* const>& Constraints = Modifier->GetConstraints();

		while (ConstraintIdx < Constraints.Num())
		{
			FPBDCollisionConstraint* CurrentConstraint = Constraints[ConstraintIdx];


			TArrayView<FManifoldPoint> ManifoldPoints = CurrentConstraint->GetManifoldPoints();
			if (ManifoldPoints.Num())
			{
				PairModifier = FContactPairModifier(CurrentConstraint, *Modifier);
				return;
			}

			// Constraint has no points, try next constraint.
			++ConstraintIdx;
		}

		// No constraints remaining.
		SetToEnd();
	}

	TArrayView<FPBDCollisionConstraint* const>& FCollisionContactModifier::GetConstraints()
	{
		return Constraints;
	}

	void FCollisionContactModifier::DisableConstraint(FPBDCollisionConstraint& Constraint)
	{
		Constraint.SetDisabled(true);
	}

	void FCollisionContactModifier::EnableConstraint(FPBDCollisionConstraint& Constraint)
	{
		Constraint.SetDisabled(false);
	}

	void FCollisionContactModifier::MarkConstraintForManifoldUpdate(FPBDCollisionConstraint& Constraint)
	{
		NeedsManifoldUpdate.Add(&Constraint);
	}

	void FCollisionContactModifier::UpdateConstraintManifolds()
	{
		for (FPBDCollisionConstraint* Constraint : NeedsManifoldUpdate)
		{
			Constraint->UpdateManifoldContacts();
		}

		NeedsManifoldUpdate.Reset();
	}
}