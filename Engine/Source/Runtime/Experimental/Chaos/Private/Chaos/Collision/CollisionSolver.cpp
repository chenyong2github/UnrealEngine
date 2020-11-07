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

		bool Chaos_Manifold_PushOut_StaticFriction = true;
		bool Chaos_Manifold_PushOut_Restitution = false;
		bool Chaos_Manifold_PushOut_PositionCorrection = true;
		bool Chaos_Manifold_PushOut_VelocityCorrection = true;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_StaticFriction(TEXT("p.Chaos.Collision.Manifold.PushOut.StaticFriction"), Chaos_Manifold_PushOut_StaticFriction, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_Restitution(TEXT("p.Chaos.Collision.Manifold.PushOut.Restitution"), Chaos_Manifold_PushOut_Restitution, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_PositionCorrection(TEXT("p.Chaos.Collision.Manifold.PushOut.PositionCorrection"), Chaos_Manifold_PushOut_PositionCorrection, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_VelocityCorrection(TEXT("p.Chaos.Collision.Manifold.PushOut.VelocityCorrection"), Chaos_Manifold_PushOut_VelocityCorrection, TEXT(""));

		float Chaos_Manifold_MinStiffness = 1.0f;
		float Chaos_Manifold_MaxStiffness = 1.0f;
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_MinStiffness(TEXT("p.Chaos.Collision.Manifold.MinStiffness"), Chaos_Manifold_MinStiffness, TEXT(""));
		FAutoConsoleVariableRef CVarChaos_Manifold_PushOut_MaxStiffness(TEXT("p.Chaos.Collision.Manifold.MaxStiffness"), Chaos_Manifold_MaxStiffness, TEXT(""));

		float Chaos_Manifold_ImpulseTolerance = 1.e-4f;
		FAutoConsoleVariableRef CVarChaos_Manifold_ImpulseTolerance(TEXT("p.Chaos.Collision.Manifold.ImpulseTolerance"), Chaos_Manifold_ImpulseTolerance, TEXT(""));

		float Chaos_Manifold_PositionTolerance = 1.e-4f;
		FAutoConsoleVariableRef CVarChaos_Manifold_PositionTolerance(TEXT("p.Chaos.Collision.Manifold.PositionTolerance"), Chaos_Manifold_PositionTolerance, TEXT(""));

		// Calculate the impulse to drive contact velocity to zero (or positiove for restitution), and handle frction.
		// The impulse is applied to the input/output velocities.
		// Some state on the in/out Manifold point is also modified for use in the push out phase.
		void CalculateManifoldVelocityCorrection(
			const FReal Stiffness,
			const FCollisionContact& Contact,
			FManifoldPoint& ManifoldPoint,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FReal InvM0,
			const FMatrix33& InvI0,
			const FReal InvM1,
			const FMatrix33& InvI1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1,
			FVec3& V0,
			FVec3& W0,
			FVec3& V1,
			FVec3& W1)
		{
			const bool bIsRigidDynamic0 = (InvM0 > 0.0f);
			const bool bIsRigidDynamic1 = (InvM1 > 0.0f);

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			// Reject non-contact points unless the point has previously been processed - we may want to undo some of the previous work
			if ((ContactPhi > Chaos_Collision_CollisionClipTolerance) && !ManifoldPoint.bActive)
			{
				return;
			}

			const FVec3 ContactVelocity0 = V0 + FVec3::CrossProduct(W0, RelativeContactPoint0);
			const FVec3 ContactVelocity1 = V1 + FVec3::CrossProduct(W1, RelativeContactPoint1);
			const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FReal ContactVelocityNormalLen = FVec3::DotProduct(ContactVelocity, ContactNormal);

			// Reject contacts moving apart unless the point has previously been processed, in which case we may want to undo some of the previous work
			if ((ContactVelocityNormalLen > 0.0f) && !ManifoldPoint.bActive)
			{
				return;
			}

			// Target normal velocity, including restitution
			const bool bApplyRestitution = (Contact.Restitution > 0.0f) && (ManifoldPoint.InitialContactVelocity < -ParticleParameters.RestitutionVelocityThreshold);
			FReal ContactVelocityTargetNormal = 0.0f;
			if (bApplyRestitution)
			{
				ContactVelocityTargetNormal = FMath::Max(0.0f, -Contact.Restitution * ManifoldPoint.InitialContactVelocity);
			}

			// Friction settings
			const FReal DynamicFriction = Contact.Friction;
			const FReal StaticFriction = FMath::Max(DynamicFriction, Contact.AngularFriction);

			// Calculate constraint-space mass
			const FMatrix33 ConstraintMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, InvM0) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, InvM1) : FMatrix33(0));

			// Calculate the impulse required to drive contact velocity to zero, including lateral movement, as if we have infinite friction
			// Impulse = ContactVelocityError / (J.M.Jt)
			const FVec3 ContactVelocityTarget = ContactVelocityTargetNormal * ContactNormal;
			const FVec3 ContactVelocityChange = ContactVelocityTarget - ContactVelocity;
			const FMatrix33 ConstraintMass = ConstraintMassInv.Inverse();
			FVec3 Impulse = Stiffness * ConstraintMass * ContactVelocityChange;

			// Clip the impulse so that the accumulated impulse is not in the wrong direction and is in the friction cone
			// Clipping the accumulated impulse instead of the incremental iteration impulse is very important for jitter
			FVec3 NetImpulse = ManifoldPoint.NetImpulse + Impulse;

			// Normal impulse
			const FReal NetImpulseNormalLen = FVec3::DotProduct(NetImpulse, ContactNormal);

			// Tangential impulse
			const FVec3 NetImpulseTangential = NetImpulse - NetImpulseNormalLen * ContactNormal;
			const FReal NetImpulseTangentialLen = NetImpulseTangential.Size();

			// Check total accumulated impulse against static friction cone
			// If within static friction cone use the already calculated impulse
			bool bInsideStaticFrictionCone = true;
			const FReal MaximumNetImpulseTangential = StaticFriction * NetImpulseNormalLen;
			if (NetImpulseTangentialLen > FMath::Max(MaximumNetImpulseTangential, KINDA_SMALL_NUMBER))
			{
				// Outside static friction cone, solve for normal relative velocity and keep tangent at cone edge
				// Note: assuming the current accumulated impulse is within the cone, then adding any vector 
				// also within the cone is guaranteed to still be in the cone. So we don't need to clip the 
				// accumulated impulse here, only the incremental impulse.
				bInsideStaticFrictionCone = false;

				// Projecting the impulse, like in the following commented out line, is a simplification that fails with fast sliding contacts.
				// ClippedNetImpulse = (MaximumImpulseTangential / ImpulseTangentialSize) * ImpulseTangential + NewAccImpNormalSize * ContactNormal;
				// I.e., reducing the tangential impulse will affect the post-impulse normal velocity, requiring a change in normal impulse, which changes the frcition cone, and so on
				FVec3 Tangent = NetImpulseTangential / NetImpulseTangentialLen;
				FReal DirectionalConstraintMassInv = FVec3::DotProduct(ContactNormal, ConstraintMassInv * (ContactNormal + DynamicFriction * Tangent));
				if (FMath::Abs(DirectionalConstraintMassInv) > SMALL_NUMBER)
				{
					FReal RelativeNormalVelocity = FVec3::DotProduct(ContactVelocityChange, ContactNormal);
					const FReal ImpulseMag = RelativeNormalVelocity / DirectionalConstraintMassInv;
					Impulse = ImpulseMag * (ContactNormal + DynamicFriction * Tangent);
				}
				else
				{
					Impulse = FVec3(0);
				}

				NetImpulse = ManifoldPoint.NetImpulse + Impulse;
			}

			// Clamp the total impulse to be positive along the normal
			const bool bActive = FVec3::DotProduct(NetImpulse, ContactNormal) > 0.0f;
			if (!bActive)
			{
				Impulse = -ManifoldPoint.NetImpulse;
				NetImpulse = FVec3(0);
			}

			// Calculate the velocity deltas from the impulse
			if (bIsRigidDynamic0)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				V0 += InvM0 * Impulse;
				W0 += InvI0 * AngularImpulse;
			}
			if (bIsRigidDynamic1)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, Impulse);
				V1 -= InvM1 * Impulse;
				W1 -= InvI1 * AngularImpulse;
			}

			// PushOut needs to know if we applied restitution and static friction
			ManifoldPoint.bActive = bActive;
			ManifoldPoint.bWasEverActive = ManifoldPoint.bWasEverActive && bActive;											// Latched to on-state
			ManifoldPoint.bInsideStaticFrictionCone = bActive && bInsideStaticFrictionCone;
			ManifoldPoint.bRestitutionEnabled = bActive && (bApplyRestitution || ManifoldPoint.bRestitutionEnabled);		// Latches to on-state
			ManifoldPoint.NetImpulse = NetImpulse;

			// If we applied any additional impulse, we need to go again next iteration
			const FReal ImpulseTolerance = Chaos_Manifold_ImpulseTolerance * Chaos_Manifold_ImpulseTolerance;
			*IterationParameters.NeedsAnotherIteration = (Impulse.SizeSquared() > ImpulseTolerance);
		}


		// @todo(chaos): Remove tangential impulses that are counteracting each other...
		//
		// Because we solve the copntacts in the manifold one at a time in a loop, the static friction calculation
		// will end up applying unecessary later impulses at each contact that counteract each other. This can
		// lead to the friction cone test failing when it should not.
		//
		// Explanation:
		// Think about a box dropping flat onto a plane. No lateral impulses are required, but actually each
		// contact will end up with a significant lateral impulse. If we think about just the first corner
		// contact we solve (which does not know there are any others) then the static friction solver will
		// calculate an impulse that sets the corner velocity to zero, both along the normal and laterally. 
		// If the impulse we just along the normal, the box would rotate leading to a non-zero lateral
		// velocity, so the actual impulse has a lateral component to counteract this. Then we move onto
		// the opposing corner (say) which calculates a similar impulse with a lateral component. The net 
		// result is a set of impulses whose lateral components cancel out, but could be close to or past the
		// static friction cone.
		//
		// This function is meant to minimize the impulses so that friction cone calculations are more accurate.
		//
		// One day it could dp something like this:
		//	Calculate tangetial axes
		//	Calculate Net tangential angular impulse about each axis from all manifold points
		//	Calculate Sum of magnitude of angular impulses about each axis from all manifold points
		//	Scale tangential angular impulses for all manifold points about each axis so that net and sum match
		//
		void FixTangentialImpulses(
			FRigidBodyPointContactConstraint& Constraint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			const FVec3& P0, // Centre of Mass Positions and Rotations
			const FRotation3& Q0,
			const FVec3& P1,
			const FRotation3& Q1)
		{
			// We only need to do this for dynamic-kinematic pairs (well really any time we have a large mass difference, but this will do for now)
			if (bIsRigidDynamic0 && bIsRigidDynamic1)
			{
				return;
			}

		}


		// Apply a position correction to the bodies so that the manifold points is not penetrating.
		// This modifies the in/out CoM position and rotations.
		void ApplyManifoldPushOutCorrection(
			const FReal Stiffness,
			const FCollisionContact& Contact,
			FManifoldPoint& ManifoldPoint,
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const bool bIsRigidDynamic0,
			const bool bIsRigidDynamic1,
			FVec3& P0, // Centre of Mass Positions and Rotations
			FRotation3& Q0,
			FVec3& P1,
			FRotation3& Q1)
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			const FReal Margin0 = 0.0f;//Contact.Implicit[0]->GetMargin();
			const FReal Margin1 = 0.0f;//Contact.Implicit[1]->GetMargin();

			// Calculate the position error we need to correct, including static friction and restitution
			// Position correction uses the deepest point on each body (see velocity correction which uses average contact)
			const FRotation3& PlaneQ = (ManifoldPoint.ContactPoint.ContactNormalOwnerIndex == 0) ? Q0 : Q1;
			const FVec3 ContactNormal = PlaneQ * ManifoldPoint.CoMContactNormal;

			const bool bApplyStaticFriction = (ManifoldPoint.bInsideStaticFrictionCone && Chaos_Manifold_PushOut_StaticFriction);
			FVec3 LocalContactPoint0 = (bApplyStaticFriction) ? ManifoldPoint.PrevCoMContactPoints[0] : ManifoldPoint.CoMContactPoints[0];
			FVec3 LocalContactPoint1 = (bApplyStaticFriction) ? ManifoldPoint.PrevCoMContactPoints[1] : ManifoldPoint.CoMContactPoints[1];

			// We could push out to the PBD distance that would give an implicit velocity equal to -(1+e).Vin
			// but the values involved are not very stable from frame to frame, so instead we actually pull
			// objects together so that Phi = 0 after pushout if the contact applied a contact impulse
			const FVec3 PhiPadding = FVec3(0);

			// Contact points on each body adjusted so that the points end up separated by TargetPhi
			const FVec3 RelativeContactPoint0 = Q0 * LocalContactPoint0 - PhiPadding - Margin0 * ContactNormal;
			const FVec3 RelativeContactPoint1 = Q1 * LocalContactPoint1 + PhiPadding + Margin1 * ContactNormal;

			// Net error we need to correct, including lateral movement to correct for friction
			const FVec3 ContactError = (P1 + RelativeContactPoint1) - (P0 + RelativeContactPoint0);
			const FReal ContactErrorNormal = FVec3::DotProduct(ContactError, ContactNormal);

			if (ContactError.SizeSquared() < Chaos_Manifold_PositionTolerance * Chaos_Manifold_PositionTolerance)
			{
				return;
			}

			// Calculate joint-space mass matrix (J.M.Jt)
			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 ContactMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, PBDRigid1->InvM()) : FMatrix33(0));

			// Calculate pushout
			// - If we were inside the static friction cone during the apply step, correct positions so that
			// the relative contact points at the start of the frame are coincident.
			// - If we were outside the static friction cone, just push out along the normal 
			// i.e., ignore dynamic friction during the pushout step
			FVec3 PushOut = FVec3(0);
			if (bApplyStaticFriction)
			{
				const FMatrix33 ContactMass = ContactMassInv.Inverse();
				PushOut = Stiffness * ContactMass * ContactError;
			}
			else
			{
				const FReal PushOutDenominator = FVec3::DotProduct(ContactNormal, ContactMassInv * ContactNormal);
				if (PushOutDenominator > SMALL_NUMBER)
				{
					PushOut = (Stiffness * ContactErrorNormal / PushOutDenominator) * ContactNormal;
				}
			}

			FVec3 NetPushOut = ManifoldPoint.NetPushOut + PushOut;

			if (bIsRigidDynamic0)
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint0, PushOut);
				const FVec3 DX0 = PBDRigid0->InvM() * PushOut;
				const FVec3 DR0 = InvI0 * AngularPushOut;

				P0 += DX0;
				Q0 += FRotation3::FromElements(DR0, 0.f) * Q0 * FReal(0.5);
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
			}

			if (bIsRigidDynamic1)
			{
				const FVec3 AngularPushOut = FVec3::CrossProduct(RelativeContactPoint1, PushOut);
				const FVec3 DX1 = -(PBDRigid1->InvM() * PushOut);
				const FVec3 DR1 = -(InvI1 * AngularPushOut);

				P1 += DX1;
				Q1 += FRotation3::FromElements(DR1, 0.f) * Q1 * FReal(0.5);
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
			}

			ManifoldPoint.NetPushOut = NetPushOut;
			*IterationParameters.NeedsAnotherIteration = true;
		}


		// Apply a velocity correction to the Manifold point for use in the PushOut phase.
		// This version is simpler than the main velocity solve - it does not worry
		// about friction for example.
		void ApplyManifoldPushOutVelocityCorrection(
			const FReal Stiffness,
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
			const FRotation3& Q1)
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			// Velocity correction uses the average contact point, and not the deepest point on each body
			const FVec3 RelativeContactPoint0 = ManifoldPoint.ContactPoint.Location - P0;
			const FVec3 RelativeContactPoint1 = ManifoldPoint.ContactPoint.Location - P1;
			const FVec3 ContactNormal = ManifoldPoint.ContactPoint.Normal;
			const FReal ContactPhi = ManifoldPoint.ContactPoint.Phi;

			const FVec3 ContactVelocity0 = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, RelativeContactPoint0);
			const FVec3 ContactVelocity1 = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, RelativeContactPoint1);
			const FVec3 ContactVelocity = ContactVelocity0 - ContactVelocity1;
			const FReal ContactVelocityNormal = FVec3::DotProduct(ContactVelocity, ContactNormal);

			// If we are already moving away we are probably done. However, we may want to undo some
			// of the previously applied velocoity correct if there is some
			if ((ContactVelocityNormal > 0.0f) && FMath::IsNearlyZero(ManifoldPoint.NetPushOutImpulse))
			{
				return;
			}

			// If we applied restitution in the velocity solve step, we also apply it here
			FReal TargetVelocityNormal = 0.0f;
			if (ManifoldPoint.bRestitutionEnabled && Chaos_Manifold_PushOut_Restitution)
			{
				TargetVelocityNormal = FMath::Max(0.0f, -Contact.Restitution * ManifoldPoint.InitialContactVelocity);
			}

			// Calculate constraint-space inverse mass
			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FMatrix33 ContactMassInv =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(RelativeContactPoint0, InvI0, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(RelativeContactPoint1, InvI1, PBDRigid1->InvM()) : FMatrix33(0));

			// Calculate the impulse to get the desired target normal velocity
			// We are ignoring both static and dynamic friction here
			FReal ImpulseNormal = 0.0f;
			const FReal ImpulseDenominator = FVec3::DotProduct(ContactNormal, ContactMassInv * ContactNormal);
			if (FMath::Abs(ImpulseDenominator) > SMALL_NUMBER)
			{
				const FReal ContactVelocityError = TargetVelocityNormal - ContactVelocityNormal;
				ImpulseNormal = (Stiffness * ContactVelocityError / ImpulseDenominator);
			}

			// If we applied a negative impulse this iteration, make sure the total impulse is not negative
			FReal NetImpulseNormal = ManifoldPoint.NetPushOutImpulse + ImpulseNormal;
			if (NetImpulseNormal < 0.0f)
			{
				ImpulseNormal = -ManifoldPoint.NetPushOutImpulse;
				NetImpulseNormal = 0.0f;
			}

			const FVec3 Impulse = ImpulseNormal * ContactNormal;
			if (bIsRigidDynamic0)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint0, Impulse);
				const FVec3 DV0 = PBDRigid0->InvM() * Impulse;
				const FVec3 DW0 = InvI0 * AngularImpulse;

				PBDRigid0->V() += DV0;
				PBDRigid0->W() += DW0;
			}

			if (bIsRigidDynamic1)
			{
				const FVec3 AngularImpulse = FVec3::CrossProduct(RelativeContactPoint1, -Impulse);
				const FVec3 DV1 = -PBDRigid1->InvM() * Impulse;
				const FVec3 DW1 = InvI1 * AngularImpulse;

				PBDRigid1->V() += DV1;
				PBDRigid1->W() += DW1;
			}

			ManifoldPoint.NetPushOutImpulse = NetImpulseNormal;
		}


		// Velocity solver loop for a single contact manifold.
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
			FVec3 V0 = Particle0->V();
			FVec3 W0 = Particle0->W();
			FVec3 V1 = Particle1->V();
			FVec3 W1 = Particle1->W();

			const FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : 0.0f;
			const FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : 0.0f;
			const FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Constraint.Manifold.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Constraint.Manifold.InvInertiaScale1 : FMatrix33(0);

			Constraint.AccumulatedImpulse = FVec3(0);

			// Velocity solve requires full stiffness for restitution to work correctly
			const FReal Stiffness = 1.0f;

			// Iterate over the manifold and accumulate velocity corrections - we will apply them after the loop
			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			for (int32 PointIndex = ManifoldPoints.Num() - 1; PointIndex >= 0; --PointIndex)
			{
				FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

				CalculateManifoldVelocityCorrection(
					Stiffness,
					Constraint.Manifold,
					ManifoldPoint,
					IterationParameters,
					ParticleParameters,
					InvM0, InvI0, InvM1, InvI1,
					P0, Q0, P1, Q1,
					V0, W0, V1, W1);

				Constraint.AccumulatedImpulse += ManifoldPoint.NetImpulse;
			}

			if (bIsRigidDynamic0)
			{
				const FVec3 DV0 = V0 - PBDRigid0->V();
				const FVec3 DW0 = W0 - PBDRigid0->W();
				const FVec3 DX0 = DV0 * IterationParameters.Dt;
				const FVec3 DR0 = DW0 * IterationParameters.Dt;

				PBDRigid0->V() = V0;
				PBDRigid0->W() = W0;
				P0 += DX0;
				Q0 += FRotation3::FromElements(DR0, 0.f) * Q0 * FReal(0.5);
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(PBDRigid0, P0, Q0);
			}

			if (bIsRigidDynamic1)
			{
				const FVec3 DV1 = V1 - PBDRigid1->V();
				const FVec3 DW1 = W1 - PBDRigid1->W();
				const FVec3 DX1 = DV1 * IterationParameters.Dt;
				const FVec3 DR1 = DW1 * IterationParameters.Dt;

				PBDRigid1->V() = V1;
				PBDRigid1->W() = W1;
				P1 += DX1;
				Q1 += FRotation3::FromElements(DR1, 0.f) * Q1 * FReal(0.5);
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
			}


			// Eliminate any tangential impulses that are opposing each other. This helps with static friction
			FixTangentialImpulses(
				Constraint,
				Particle0,
				Particle1,
				IterationParameters,
				ParticleParameters,
				bIsRigidDynamic0,
				bIsRigidDynamic1,
				P0, Q0, P1, Q1);
		}


		// Pushout solver loop for a single contafct manifold.
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

			// Gradually increase position correction through iterations (optional based on cvars)
			const FReal Interpolant = (FReal)(IterationParameters.Iteration + 1) / (FReal)IterationParameters.NumIterations;
			const FReal Stiffness = FMath::Lerp(Chaos_Manifold_MinStiffness, Chaos_Manifold_MaxStiffness, Interpolant);

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			TArrayView<FManifoldPoint> ManifoldPoints = Constraint.GetManifoldPoints();
			
			// Apply the position correction so that all contacts have zero separation
			if (Chaos_Manifold_PushOut_PositionCorrection)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					if (ManifoldPoint.bActive || (ManifoldPoint.ContactPoint.Phi < 0.0f))
					{
						ApplyManifoldPushOutCorrection(
							Stiffness,
							Constraint.Manifold,
							ManifoldPoint,
							Particle0,
							Particle1,
							IterationParameters,
							ParticleParameters,
							bIsRigidDynamic0,
							bIsRigidDynamic1,
							P0, Q0, P1, Q1);
					}
				}
			}

			// Apply a velocity correction so that all contacts have non-negative contact velocity
			// @todo(chaos): this does not modify position, so we should precalculate inertia
			if (Chaos_Manifold_PushOut_VelocityCorrection)
			{
				for (int32 PointIndex = 0; PointIndex < ManifoldPoints.Num(); ++PointIndex)
				{
					FManifoldPoint& ManifoldPoint = Constraint.SetActiveManifoldPoint(PointIndex, P0, Q0, P1, Q1);

					if (ManifoldPoint.bActive || !ManifoldPoint.NetPushOut.IsNearlyZero())
					{
						ApplyManifoldPushOutVelocityCorrection(
							Stiffness,
							Constraint.Manifold,
							ManifoldPoint,
							Particle0,
							Particle1,
							IterationParameters,
							ParticleParameters,
							bIsRigidDynamic0,
							bIsRigidDynamic1,
							P0, Q0, P1, Q1);
					}
				}
			}
		}

	}
}