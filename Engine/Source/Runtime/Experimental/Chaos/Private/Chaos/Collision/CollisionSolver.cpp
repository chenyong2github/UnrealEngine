// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/CollisionSolver.h"

#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Defines.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

CHAOS_API DECLARE_LOG_CATEGORY_EXTERN(LogChaosCollisionSolver, Log, All);
DEFINE_LOG_CATEGORY(LogChaosCollisionSolver);

namespace Chaos
{
	namespace Collisions
	{
		extern int32 Chaos_Collision_EnergyClampEnabled;
		extern int32 Chaos_Collision_UseShockPropagation;
		extern float Chaos_Collision_CollisionClipTolerance;

		bool ChaosManifoldPushOutStaticFriction = true;
		bool ChaosManifoldPushOutRestitution = false;
		FAutoConsoleVariableRef CVarChaosManifoldPushOutStaticFriction(TEXT("p.Chaos.Collision.ManifoldPushOutStaticFriction"), ChaosManifoldPushOutStaticFriction, TEXT(""));
		FAutoConsoleVariableRef CVarChaosManifoldPushOutRestitution(TEXT("p.Chaos.Collision.ManifoldPushOutRestitution"), ChaosManifoldPushOutRestitution, TEXT(""));


		void CalculateManifoldVelocityCorrection(
			const FCollisionContact& Contact,
			FManifoldPoint& ManifoldPoint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			bool bInApplyRestitution,
			bool bInApplyFriction,
			FVec3& DV0, // Out
			FVec3& DW0, // Out
			FVec3& DV1, // Out
			FVec3& DW1) // Out
		{
			DV0 = FVec3(0);
			DW0 = FVec3(0);
			DV1 = FVec3(0);
			DW1 = FVec3(0);

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			bool bProcessContact = (ContactPhi < Chaos_Collision_CollisionClipTolerance) || !ManifoldPoint.NetImpulse.IsNearlyZero();
			if (!bProcessContact)
			{
				return;
			}

			// Do not early out on negative normal velocity since there can still be an accumulated impulse
			*IterationParameters.NeedsAnotherIteration = true;

			const FVec3 Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, RelativeContactPoint0);
			const FVec3 Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, RelativeContactPoint1);
			const FVec3 RelativeVelocity = Body1Velocity - Body2Velocity;  
			
			FReal RelativeNormalVelocityForRestitution = FVec3::DotProduct(RelativeVelocity, ContactNormal); // Relative velocity in direction of the normal as used by restitution
			FReal RelativeVelocityForRestitutionSize = RelativeVelocity.Size();
			// Use the previous contact velocities to calculate the restitution response
			//if (Chaos_Collision_PrevVelocityRestitutionEnabled && bInApplyRestitution && Contact.Restitution > (FReal)0.0f)
			//{
			//	RelativeNormalVelocityForRestitution = CalculateRelativeNormalVelocityForRestitution(Particle0, Particle1, Q0, Q1, ContactNormal, VectorToPoint1, VectorToPoint2, RelativeVelocityForRestitutionSize);
			//}

			// Resting contact if very close to the surface
			const bool bApplyFriction = bInApplyFriction;
			const bool bApplyRestitution = false;//bInApplyRestitution && (RelativeVelocityForRestitutionSize > ParticleParameters.RestitutionVelocityThreshold);
			const FReal Restitution = (bApplyRestitution) ? Contact.Restitution : (FReal)0;
			const FReal DynamicFriction = bApplyFriction ? Contact.Friction : (FReal)0;
			const FReal StaticFriction = bApplyFriction ? FMath::Max(DynamicFriction, Contact.AngularFriction) : (FReal)0;

			// PushOut needs to know if we applied restitution and static friction
			// Restitution is a latch - did we apply it in any iteration?
			// Static friction is cumulative - did we end up inside the friction cone? Updated below if we leave cone.
			ManifoldPoint.bInsideStaticFrictionCone = bApplyFriction;
			ManifoldPoint.bRestitutionEnabled = bApplyRestitution || ManifoldPoint.bRestitutionEnabled;
			ManifoldPoint.bActive = true;

			// Calculate constraint-space mass
			const FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 ConstraintMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, WorldSpaceInvI1, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, WorldSpaceInvI2, PBDRigid1->InvM()) : FMatrix33(0));

			// Calculate the impulse required to drive contact velocity to zero, including lateral movement
			// Impulse = ContactVelocityError / (J.M.Jt)
			const FVec3 VelocityTarget = (-Restitution * RelativeNormalVelocityForRestitution) * ContactNormal;
			const FVec3 VelocityChange = VelocityTarget - RelativeVelocity;
			const FMatrix33 ConstraintMass = ConstraintMassInv.Inverse();
			FVec3 Impulse = ConstraintMass * VelocityChange;

			// Clip the impulse so that the accumulated impulse is not in the wrong direction and in the friction cone
			// Clipping the accumulated impulse instead of the delta impulse (for the current iteration) is very important for jitter
			const FVec3 UnclippedAccumulatedImpulse = ManifoldPoint.NetImpulse + Impulse;

			// Normal impulse
			const FReal AccumulatedImpulseNormalSize = FVec3::DotProduct(UnclippedAccumulatedImpulse, ContactNormal);

			// Tangential impulse
			const FVec3 AccumulatedImpulseTangential = UnclippedAccumulatedImpulse - AccumulatedImpulseNormalSize * ContactNormal;
			const FReal AccumulatedImpulseTangentialSize = AccumulatedImpulseTangential.Size();

			// Check total accumulated impulse against static friction cone
			// If within static friction cone use the already calculated impulse
			const FReal MaximumAccumulatedImpulseTangential = StaticFriction * AccumulatedImpulseNormalSize;
			if (AccumulatedImpulseTangentialSize > FMath::Max(MaximumAccumulatedImpulseTangential, KINDA_SMALL_NUMBER))
			{
				// Outside static friction cone, solve for normal relative velocity and keep tangent at cone edge
				// Note: assuming the current accumulated impulse is within the cone, then adding any vector 
				// also within the cone is guaranteed to still be in the cone. So we don't need to clip the 
				// accumulated impulse here, only the incremental impulse.
				if (bApplyFriction)
				{
					ManifoldPoint.bInsideStaticFrictionCone = false;
				}

				// Projecting the impulse, like in the following commented out line, is a simplification that fails with fast sliding contacts.
				// ClippedAccumulatedImpulse = (MaximumImpulseTangential / ImpulseTangentialSize) * ImpulseTangential + NewAccImpNormalSize * ContactNormal;
				// I.e., reducing the tangential impulse will affect the post-impulse normal velocity, requiring a change in normal impulse, which changes the frcition cone, and so on
				FVec3 Tangent = AccumulatedImpulseTangential / AccumulatedImpulseTangentialSize;
				FReal DirectionalConstraintMassInv = FVec3::DotProduct(ContactNormal, ConstraintMassInv * (ContactNormal + DynamicFriction * Tangent));
				if (FMath::Abs(DirectionalConstraintMassInv) > SMALL_NUMBER)
				{
					FReal RelativeNormalVelocity = FVec3::DotProduct(VelocityChange, ContactNormal);
					const FReal ImpulseMag = RelativeNormalVelocity / DirectionalConstraintMassInv;
					Impulse = ImpulseMag * (ContactNormal + DynamicFriction * Tangent);
				}
				else
				{
					Impulse = FVec3(0);
				}
			}

			// Clamp the total impulse to be positive along the normal
			FVec3 NewAccumulatedImpulse = ManifoldPoint.NetImpulse + Impulse;
			if (FVec3::DotProduct(NewAccumulatedImpulse, ContactNormal) <= (FReal)0)
			{
				Impulse = -ManifoldPoint.NetImpulse;
				NewAccumulatedImpulse = FVec3(0);
				ManifoldPoint.bActive = false;
			}
			ManifoldPoint.NetImpulse = NewAccumulatedImpulse;

			// @todo(chaos): should this clamp the accumulated impulse?
			if (Chaos_Collision_EnergyClampEnabled != 0)
			{
				// Clamp the delta impulse to make sure we don't gain kinetic energy (ignore potential energy)
				// This should not modify the output impulses very often
				Impulse = GetEnergyClampedImpulse(Particle0->CastToRigidParticle(), Particle1->CastToRigidParticle(), Impulse, RelativeContactPoint0, RelativeContactPoint1, Body1Velocity, Body2Velocity);
			}

			if (bIsRigidDynamic0)
			{
				// Velocity update for next step
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				DV0 = PBDRigid0->InvM() * Impulse;
				DW0 = WorldSpaceInvI1 * NetAngularImpulse;
			}

			if (bIsRigidDynamic1)
			{
				// Velocity update for next step
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, -Impulse);
				DV1 = -PBDRigid1->InvM() * Impulse;
				DW1 = WorldSpaceInvI2 * NetAngularImpulse;
			}
		}

		void CalculateManifoldPushOutCorrection(
			const FCollisionContact& Contact,		// @todo(chaos): clean up - split out contact settings from contact state
			FManifoldPoint& ManifoldPoint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& DX0, // Out
			FVec3& DR0, // Out
			FVec3& DX1, // Out
			FVec3& DR1) // Out
		{
			DX0 = FVec3(0);
			DR0 = FVec3(0);
			DX1 = FVec3(0);
			DR1 = FVec3(0);

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			// If we used restitution in the Apply step, we need to make sure that PushOut enforces the same distance that
			// the velocity solve step should have applied if it fully resolved
			FReal ContactResitutionPadding = 0.0f;
			if (ChaosManifoldPushOutRestitution)
			{
				// For zero restitution we put back at original distance
				ContactResitutionPadding = ManifoldPoint.InitialPhi;	
				if (ManifoldPoint.bRestitutionEnabled)
				{
					ContactResitutionPadding = -(1.0f + Contact.Restitution) * (ManifoldPoint.InitialPhi - ManifoldPoint.PrevPhi) - ManifoldPoint.PrevPhi;
				}
			}
			ContactResitutionPadding = FMath::Max(0.0f, ContactResitutionPadding);

			// Calculate the position error we need to correct, including static friction and restitution
			// Position correction uses the deepest point on each body (see velocity correction which uses average contact)
			const bool bApplyStaticFriction = (ManifoldPoint.bInsideStaticFrictionCone && ChaosManifoldPushOutStaticFriction);
			const FVec3 LocalContactPoint1 = bApplyStaticFriction ? ManifoldPoint.PrevCoMContactPoint1 : ManifoldPoint.CoMContactPoints[1];
			const FVec3 RelativeContactPoint0 = Q0 * ManifoldPoint.CoMContactPoints[0];
			const FVec3 RelativeContactPoint1 = Q1 * LocalContactPoint1;
			const FVec3 ContactNormal = Q1 * ManifoldPoint.CoMContactNormal;
			const FVec3 ContactError = (P1 + RelativeContactPoint1) - (P0 + RelativeContactPoint0) + ContactResitutionPadding * ContactNormal;

			// We never pull the contact together here unless we have already applied some positive correction, in which case we may undo some of that
			const FReal ContactErrorNormal = FVec3::DotProduct(ContactError, ContactNormal);
			if ((ContactErrorNormal < 0.0f) && ManifoldPoint.NetPushOut.IsNearlyZero())
			{
				return;
			}

			// Do not early out on negative normal velocity since there can still be an accumulated impulse
			*IterationParameters.NeedsAnotherIteration = true;

			// Calculate joint-space mass matrix (J.M.Jt)
			const FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 Factor =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, WorldSpaceInvI1, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, WorldSpaceInvI2, PBDRigid1->InvM()) : FMatrix33(0));

			// Calculate pushout
			// - If we were inside the static friction cone during the apply step, correct positions so that
			// the relative contact points at the start of the frame are coincident.
			// - If we were outside the static friction cone, just push out along the normal 
			// i.e., ignore dynamic friction during the pushout step
			FVec3 PushOut = FVec3(0);
			if (bApplyStaticFriction)
			{
				const FMatrix33 FactorInverse = Factor.Inverse();
				PushOut = FactorInverse * ContactError;
			}
			else
			{
				const FReal PushOutDenominator = FVec3::DotProduct(ContactNormal, Factor * ContactNormal);
				if (PushOutDenominator > SMALL_NUMBER)
				{
					PushOut = ContactError / PushOutDenominator;
				}
			}

			// Ensure that the accumulated pushout on this contact point is positive.
			// We are allowed to have negative pushout in an iteration if it partially undoes
			// some previous positive pushout.
			FVec3 NewNetPushOut = ManifoldPoint.NetPushOut + PushOut;
			if (FVec3::DotProduct(NewNetPushOut, ContactNormal) <= (FReal)0)
			{
				// @todo(chaos): Maybe we should only undo the normal component here, since we still want to 
				// retain any static friction matching?
				NewNetPushOut = FVec3(0);
				PushOut = -ManifoldPoint.NetPushOut;
			}
			ManifoldPoint.NetPushOut = NewNetPushOut;

			if (bIsRigidDynamic0)
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint0, PushOut);
				DX0 = PBDRigid0->InvM() * PushOut;
				DR0 = WorldSpaceInvI1 * AngularPushOut;
			}

			if (bIsRigidDynamic1)
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint1, PushOut);
				DX1 = -(PBDRigid1->InvM() * PushOut);
				DR1 = -(WorldSpaceInvI2 * AngularPushOut);
			}
		}


		void CalculateManifoldPushOutVelocityCorrection(
			const FCollisionContact& Contact,
			FManifoldPoint& ManifoldPoint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& DV0, // Out
			FVec3& DW0, // Out
			FVec3& DV1, // Out
			FVec3& DW1) // Out
		{
			DV0 = FVec3(0);
			DW0 = FVec3(0);
			DV1 = FVec3(0);
			DW1 = FVec3(0);

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			const FVec3 Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, RelativeContactPoint0);
			const FVec3 Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, RelativeContactPoint1);
			const FVec3 RelativeVelocity = Body1Velocity - Body2Velocity;

			const FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 Factor =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, WorldSpaceInvI1, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, WorldSpaceInvI2, PBDRigid1->InvM()) : FMatrix33(0));

			// Clip the impulse so that the accumulated impulse is not in the wrong direction and in the friction cone
			// Clipping the accumulated impulse instead of the delta impulse (for the current iteration) is very important for jitter
			// Clipping impulses to be positive and contacts to be penetrating or touching
			bool bProcessContact = (ContactPhi < Chaos_Collision_CollisionClipTolerance) || !ManifoldPoint.NetImpulse.IsNearlyZero();
			if (!bProcessContact)
			{
				return;
			}

			FVec3 Impulse = FVec3(0);
			FReal ImpulseDenominator = FVec3::DotProduct(ContactNormal, Factor * ContactNormal);
			if (FMath::Abs(ImpulseDenominator) > SMALL_NUMBER)
			{
				const FVec3 VelocityChange = -RelativeVelocity;
				FReal RelativeNormalVelocity = FVec3::DotProduct(VelocityChange, ContactNormal);
				const FReal ImpulseMag = RelativeNormalVelocity / ImpulseDenominator;
				Impulse = ImpulseMag * ContactNormal;
			}

			if (bIsRigidDynamic0)
			{
				// Velocity update for next step
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				DV0 = PBDRigid0->InvM() * Impulse;
				DW0 = WorldSpaceInvI1 * NetAngularImpulse;
			}

			if (bIsRigidDynamic1)
			{
				// Velocity update for next step
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, -Impulse);
				DV1 = -PBDRigid1->InvM() * Impulse;
				DW1 = WorldSpaceInvI2 * NetAngularImpulse;
			}
		}


		void ApplyContactManifold(
			FRigidBodyPointContactConstraint& Constraint,
			TGenericParticleHandle<FReal, 3> Particle0,
			TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			Constraint.AccumulatedImpulse = FVec3(0);

			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			for (int32 PointIndex = ManifoldPoints.Num() - 1; PointIndex >= 0; --PointIndex)
			{
				FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

				FVec3 DV0, DW0, DV1, DW1;
				CalculateManifoldVelocityCorrection(
					Constraint.Manifold,
					ManifoldPoint,
					Particle0,
					Particle1,
					IterationParameters,
					ParticleParameters,
					bIsRigidDynamic0,
					bIsRigidDynamic1,
					P0, Q0, P1, Q1,
					true, true,
					DV0, DW0, DV1, DW1);

				if (bIsRigidDynamic0)
				{
					PBDRigid0->V() += DV0;
					PBDRigid0->W() += DW0;
					P0 += (DV0 * IterationParameters.Dt);
					Q0 += FRotation3::FromElements(DW0, 0.f) * Q0 * IterationParameters.Dt * FReal(0.5);
					Q0.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid0, P0, Q0);
				}

				if (bIsRigidDynamic1)
				{
					PBDRigid1->V() += DV1;
					PBDRigid1->W() += DW1;
					P1 += (DV1 * IterationParameters.Dt);
					Q1 += FRotation3::FromElements(DW1, 0.f) * Q1 * IterationParameters.Dt * FReal(0.5);
					Q1.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
				}

				Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
			}
		}


		void ApplyPushOutManifold(
			FRigidBodyPointContactConstraint& Constraint,
			const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[1]);

			bool IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Particle0->GeometryParticleHandle());
			bool IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Particle1->GeometryParticleHandle());
			// In the case of two objects which are at the same level in shock propagation which end
			// up in contact with each other, treat each object as not temporarily static. This can
			// happen, for example, at the center of an arch, or between objects which are sliding into
			// each other on a static surface.
			if ((IsTemporarilyStatic0 && IsTemporarilyStatic1) || !Chaos_Collision_UseShockPropagation)
			{
				IsTemporarilyStatic0 = false;
				IsTemporarilyStatic1 = false;
			}

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && (PBDRigid0->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic0;
			const bool bIsRigidDynamic1 = PBDRigid1 && (PBDRigid1->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic1;

			if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
			{
				return;
			}

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
			{
				// Position correction
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					if (ManifoldPoint.ContactPoint.Phi >= 0.0f)
					{
						continue;
					}

					FVec3 DX0, DR0, DX1, DR1;
					CalculateManifoldPushOutCorrection(
						Constraint.Manifold,
						ManifoldPoint,
						Particle0,
						Particle1,
						IterationParameters,
						ParticleParameters,
						bIsRigidDynamic0,
						bIsRigidDynamic1,
						P0, Q0, P1, Q1,
						DX0, DR0, DX1, DR1);

					if (bIsRigidDynamic0)
					{
						P0 += DX0;
						Q0 += FRotation3::FromElements(DR0, 0.f) * Q0 * FReal(0.5);
						Q0.Normalize();
						FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
					}
					if (bIsRigidDynamic1)
					{
						P1 += DX1;
						Q1 += FRotation3::FromElements(DR1, 0.f) * Q1 * FReal(0.5);
						Q1.Normalize();
						FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
					}
				}

				// Velocity correction
				{
					// Note: setting the active point updates the world-space state
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);
					FCollisionContact& Contact = Constraint.Manifold;

					FVec3 DV0, DW0, DV1, DW1;
					CalculateManifoldPushOutVelocityCorrection(
						Constraint.Manifold,
						ManifoldPoint,
						Particle0,
						Particle1,
						IterationParameters,
						ParticleParameters,
						bIsRigidDynamic0,
						bIsRigidDynamic1,
						P0, Q0, P1, Q1,
						DV0, DW0, DV1, DW1);

					if (bIsRigidDynamic0)
					{
						PBDRigid0->V() += DV0;
						PBDRigid0->W() += DW0;
					}

					if (bIsRigidDynamic1)
					{
						PBDRigid1->V() += DV1;
						PBDRigid1->W() += DW1;
					}
				}
			}
		}

	}
}