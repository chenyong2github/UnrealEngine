// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/Collision/CollisionSolver.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Defines.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
#if INTEL_ISPC
	extern bool bChaos_Collision_ISPC_Enabled;
#endif

	namespace Collisions
	{
		int32 Chaos_Collision_EnergyClampEnabled = 1;
		FAutoConsoleVariableRef CVarChaosCollisionEnergyClampEnabled(TEXT("p.Chaos.Collision.EnergyClampEnabled"), Chaos_Collision_EnergyClampEnabled, TEXT("Whether to use energy clamping in collision apply step"));

		int32 Chaos_Collision_RelaxationEnabled = 0; // TODO remove this feature soon
		FAutoConsoleVariableRef CVarChaosCollisionRelaxationEnabled(TEXT("p.Chaos.Collision.RelaxationEnabled"), Chaos_Collision_RelaxationEnabled, TEXT("Whether to reduce applied impulses during iterations for improved solver stability but reduced convergence"));

		int32 Chaos_Collision_PrevVelocityRestitutionEnabled = 0;
		FAutoConsoleVariableRef CVarChaosCollisionPrevVelocityRestitutionEnabled(TEXT("p.Chaos.Collision.PrevVelocityRestitutionEnabled"), Chaos_Collision_PrevVelocityRestitutionEnabled, TEXT("If enabled restitution will be calculated on previous frame velocities instead of current frame velocities"));

		int32 Chaos_Collision_ForceApplyType = 0;
		FAutoConsoleVariableRef CVarChaosCollisionAlternativeApply(TEXT("p.Chaos.Collision.ForceApplyType"), Chaos_Collision_ForceApplyType, TEXT("Force Apply step to use Velocity(1) or Position(2) modes"));

		float Chaos_Collision_ContactMovementAllowance = 0.05f;
		FAutoConsoleVariableRef CVarChaosCollisionContactMovementAllowance(TEXT("p.Chaos.Collision.AntiJitterContactMovementAllowance"), Chaos_Collision_ContactMovementAllowance, 
			TEXT("If a contact is close to where it was during a previous iteration, we will assume it is the same contact that moved (to reduce jitter). Expressed as the fraction of movement distance and Centre of Mass distance to the contact point"));

		int32 Chaos_Collision_UseAccumulatedImpulseClipSolve = 0; // This requires multiple contact points per iteration per pair and contact points that don't move too much (in body space) to have an effect
		FAutoConsoleVariableRef CVarChaosCollisionImpulseClipSolve(TEXT("p.Chaos.Collision.UseAccumulatedImpulseClipSolve"), Chaos_Collision_UseAccumulatedImpulseClipSolve, TEXT("Use experimental Accumulated impulse clipped contact solve"));

		int32 Chaos_Collision_UseShockPropagation = 1;
		FAutoConsoleVariableRef CVarChaosCollisionUseShockPropagation(TEXT("p.Chaos.Collision.UseShockPropagation"), Chaos_Collision_UseShockPropagation, TEXT(""));

		FReal Chaos_Collision_CollisionClipTolerance = 0.1f;
		FAutoConsoleVariableRef CVarChaosCollisionClipTolerance(TEXT("p.Chaos.Collision.ClipTolerance"), Chaos_Collision_CollisionClipTolerance, TEXT(""));

		bool Chaos_Collision_CheckManifoldComplete = false;
		FAutoConsoleVariableRef CVarChaosCollisionCheckManifoldComplete(TEXT("p.Chaos.Collision.CheckManifoldComplete"), Chaos_Collision_CheckManifoldComplete, TEXT(""));

		extern void UpdateManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FReal CullDistance)
		{
			const FRigidTransform3 Transform0 = GetTransform(Constraint.Particle[0]);
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);

			UpdateManifold(Constraint, Transform0, Transform1, CullDistance);
		}

		void Update(FRigidBodyPointContactConstraint& Constraint, const FReal CullDistance, const FReal Dt)
		{
			const FRigidTransform3 Transform0 = GetTransform(Constraint.Particle[0]);
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);

			Constraint.ResetPhi(CullDistance);
			UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest>(Constraint, Transform0, Transform1, CullDistance, Dt);
		}

		void Update(FRigidBodySweptPointContactConstraint& Constraint, const FReal CullDistance, const FReal Dt)
		{
			// Update as a point constraint (base class).
			Constraint.bShouldTreatAsSinglePoint = true;
			Update(*Constraint.As<FRigidBodyPointContactConstraint>(), CullDistance, Dt);
		}

		void Update(FRigidBodyMultiPointContactConstraint& Constraint, const FReal CullDistance, const FReal Dt)
		{
			const FRigidTransform3 Transform0 = GetTransform(Constraint.Particle[0]);
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);

			Constraint.ResetPhi(CullDistance);
			UpdateConstraintFromManifold(Constraint, Transform0, Transform1, CullDistance, Dt);
		}

		void ApplyAngularFriction(
			  FVec3& Impulse // In and Out
			, FVec3& AngularImpulse // In and Out
			, FReal AngularFriction
			, const FVec3& RelativeAngularVelocity
			, const FVec3& ContactVelocityChange
			, const FVec3& ContactNormal
			, const FVec3& VectorToPoint1
			, const FVec3& VectorToPoint2
			, const TPBDRigidParticleHandle<FReal, 3>* PBDRigid0
			, const TPBDRigidParticleHandle<FReal, 3>* PBDRigid1
			, bool bIsInfiniteMass0
			, bool bIsInfiniteMass1
			, const FRotation3& Q0
			, const FRotation3& Q1
			, const FMatrix33& WorldSpaceInvI1
			, const FMatrix33& WorldSpaceInvI2
			)
		{
			FReal AngularNormal = FVec3::DotProduct(RelativeAngularVelocity, ContactNormal);
			FVec3 AngularTangent = RelativeAngularVelocity - AngularNormal * ContactNormal;
			FReal NormalVelocityChange = FVec3::DotProduct(ContactVelocityChange, ContactNormal);
			FVec3 FinalAngularVelocity = FMath::Sign(AngularNormal) * FMath::Max((FReal)0, FMath::Abs(AngularNormal) - AngularFriction * NormalVelocityChange) * ContactNormal + FMath::Max((FReal)0, AngularTangent.Size() - AngularFriction * NormalVelocityChange) * AngularTangent.GetSafeNormal();
			FVec3 Delta = FinalAngularVelocity - RelativeAngularVelocity;
			if (bIsInfiniteMass0 && !bIsInfiniteMass1)
			{
				FMatrix33 WorldSpaceI2 = Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->I());
				FVec3 ImpulseDelta = PBDRigid1->M() * FVec3::CrossProduct(VectorToPoint2, Delta);
				Impulse += ImpulseDelta;
				AngularImpulse += WorldSpaceI2 * Delta - FVec3::CrossProduct(VectorToPoint2, ImpulseDelta);
			}
			else if (!bIsInfiniteMass0 && bIsInfiniteMass1)
			{
				FMatrix33 WorldSpaceI1 = Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->I());
				FVec3 ImpulseDelta = PBDRigid0->M() * FVec3::CrossProduct(VectorToPoint1, Delta);
				Impulse += ImpulseDelta;
				AngularImpulse += WorldSpaceI1 * Delta - FVec3::CrossProduct(VectorToPoint1, ImpulseDelta);
			}
			else if (!bIsInfiniteMass0 && !bIsInfiniteMass1)
			{
				FMatrix33 Cross1(0, VectorToPoint1.Z, -VectorToPoint1.Y, -VectorToPoint1.Z, 0, VectorToPoint1.X, VectorToPoint1.Y, -VectorToPoint1.X, 0);
				FMatrix33 Cross2(0, VectorToPoint2.Z, -VectorToPoint2.Y, -VectorToPoint2.Z, 0, VectorToPoint2.X, VectorToPoint2.Y, -VectorToPoint2.X, 0);
				FMatrix33 CrossI1 = Cross1 * WorldSpaceInvI1;
				FMatrix33 CrossI2 = Cross2 * WorldSpaceInvI2;
				FMatrix33 Diag1 = CrossI1 * Cross1.GetTransposed() + CrossI2 * Cross2.GetTransposed();
				Diag1.M[0][0] += PBDRigid0->InvM() + PBDRigid1->InvM();
				Diag1.M[1][1] += PBDRigid0->InvM() + PBDRigid1->InvM();
				Diag1.M[2][2] += PBDRigid0->InvM() + PBDRigid1->InvM();
				FMatrix33 OffDiag1 = (CrossI1 + CrossI2) * -1;
				FMatrix33 Diag2 = (WorldSpaceInvI1 + WorldSpaceInvI2).Inverse();
				FMatrix33 OffDiag1Diag2 = OffDiag1 * Diag2;
				FVec3 ImpulseDelta = FMatrix33((Diag1 - OffDiag1Diag2 * OffDiag1.GetTransposed()).Inverse()) * ((OffDiag1Diag2 * -1) * Delta);
				Impulse += ImpulseDelta;
				AngularImpulse += Diag2 * (Delta - FMatrix33(OffDiag1.GetTransposed()) * ImpulseDelta);
			}
		}

		// The Relaxation factor has an effect of reducing overshoot in solutions, but it will decrease the rate of convergence
		// Example: Without relaxation a cube falling directly on its face at high velocity will create an overly large impulse on the first few contacts
		// causing it to roll off in a random direction.
		// Todo: Relaxation may be removed when tracking individual contact point's accumulated impulses (e.g. manifolds), since the over-reaction will be corrected
		FReal CalculateRelaxationFactor(const FContactIterationParameters& IterationParameters)
		{
			const FReal RelaxationNumerator = FMath::Min((FReal)(IterationParameters.Iteration + 2), (FReal)IterationParameters.NumIterations);
			FReal RelaxationFactor = RelaxationNumerator / (FReal)IterationParameters.NumIterations;
			return RelaxationFactor;
		}

		FReal CalculateRelativeNormalVelocityForRestitution(
			const TGenericParticleHandle<FReal, 3> Particle0,
			const TGenericParticleHandle<FReal, 3> Particle1,
			const FRotation3& Q0,
			const FRotation3& Q1,
			const FVec3& ContactNormal,
			const FVec3& VectorToPoint1,
			const FVec3& VectorToPoint2,
			FReal &outVelocitySize)
		{
			// Get previous world space position of the contact points relative the CoM
			// Note: These particular points might not even have been in contact at the start of the frame
			// VectorToContact point is transformed to local space, and then back to world space at the previous frame orientation
			FRotation3 R0 = FParticleUtilitiesXR::GetCoMWorldRotation(Particle0); // Previous Rotation
			FRotation3 R1 = FParticleUtilitiesXR::GetCoMWorldRotation(Particle1); // Previous Rotation

			const FVec3 VectorToPoint1Prev = R0 * FRotation3::Conjugate(Q0) * VectorToPoint1;
			const FVec3 VectorToPoint2Prev = R1 * FRotation3::Conjugate(Q1) * VectorToPoint2;
			// 
			const FVec3 ContactBody1VelocityPrev = FParticleUtilities::GetPreviousVelocityAtCoMRelativePosition(Particle0, VectorToPoint1Prev);
			const FVec3 ContactBody2VelocityPrev = FParticleUtilities::GetPreviousVelocityAtCoMRelativePosition(Particle1, VectorToPoint2Prev);
			const FVec3 RelativeVelocityPrev = ContactBody1VelocityPrev - ContactBody2VelocityPrev;
			const FReal RelativeNormalVelocityForRestitution = FVec3::DotProduct(RelativeVelocityPrev, ContactNormal); // Note: using the current contact normal
			outVelocitySize = RelativeVelocityPrev.Size();
			
			return RelativeNormalVelocityForRestitution;
		}

		// Calculate velocity corrections due to the given contact
		// This function uses AccumulatedImpulse Clipping, so 
		// AccumulatedImpulse is both an input and an output
		void CalculateContactVelocityCorrections(
			FCollisionContact& Contact, // Flags may be modified
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
			bool bInApplyAngularFriction,
			bool bInApplyRelaxation,
			FVec3& AccumulatedImpulse, // InOut
			FVec3& DV0, // Out
			FVec3& DW0, // Out
			FVec3& DV1, // Out
			FVec3& DW1) // Out
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			const bool bOneWayInteraction0 = bIsRigidDynamic0 && PBDRigid0->OneWayInteraction();
			const bool bOneWayInteraction1 = bIsRigidDynamic1 && PBDRigid1->OneWayInteraction();
			const bool bInfiniteMass0 = !bIsRigidDynamic0 || (!bOneWayInteraction0 && bOneWayInteraction1);
			const bool bInfiniteMass1 = !bIsRigidDynamic1 || (bOneWayInteraction0 && !bOneWayInteraction1);

			const FVec3 ZeroVector = FVec3(0);

			const FVec3 VectorToPoint1 = Contact.Location - P0;
			const FVec3 VectorToPoint2 = Contact.Location - P1;
			const FVec3 Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, VectorToPoint1);
			const FVec3 Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, VectorToPoint2);
			const FVec3 RelativeVelocity = Body1Velocity - Body2Velocity;  // Do not early out on negative normal velocity since there can still be an accumulated impulse
			*IterationParameters.NeedsAnotherIteration = true;

			const FMatrix33 WorldSpaceInvI1 = !bInfiniteMass0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			const FMatrix33 WorldSpaceInvI2 = !bInfiniteMass1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			const FReal InvM1 = !bInfiniteMass0 ? PBDRigid0->InvM() : 0.f;
			const FReal InvM2 = !bInfiniteMass1 ? PBDRigid1->InvM() : 0.f;
			const FMatrix33 Factor =
			    (!bInfiniteMass0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, InvM1) : FMatrix33(0)) +
			    (!bInfiniteMass1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, InvM2) : FMatrix33(0));
			FVec3 AngularImpulse(0);

			FReal RelativeNormalVelocityForRestitution = FVec3::DotProduct(RelativeVelocity, Contact.Normal); // Relative velocity in direction of the normal as used by restitution
			FReal RelativeVelocityForRestitutionSize = RelativeVelocity.Size();
			// Use the previous contact velocities to calculate the restitution response
			if (Chaos_Collision_PrevVelocityRestitutionEnabled && bInApplyRestitution && Contact.Restitution > (FReal)0.0f)
			{
				RelativeNormalVelocityForRestitution = CalculateRelativeNormalVelocityForRestitution(Particle0, Particle1, Q0, Q1, Contact.Normal, VectorToPoint1, VectorToPoint2, RelativeVelocityForRestitutionSize);
			}

			// Resting contact if very close to the surface
			const bool bApplyRestitution = bInApplyRestitution && (RelativeVelocityForRestitutionSize > ParticleParameters.RestitutionVelocityThreshold);
			const FReal Restitution = (bApplyRestitution) ? Contact.Restitution : (FReal)0;
			const FReal Friction = bInApplyFriction ? Contact.Friction : (FReal)0; // Add friction even when pushing out
			const FReal AngularFriction = bInApplyAngularFriction ? Contact.AngularFriction : (FReal)0; // Don't add angular friction in pushout since we don't have accumulated angular impulse clipping, todo: experiment with this later

			const FVec3 VelocityTarget = (-Restitution * RelativeNormalVelocityForRestitution) * Contact.Normal;
			const FVec3 VelocityChange = VelocityTarget - RelativeVelocity;
			const FMatrix33 FactorInverse = Factor.Inverse();
			FVec3 Impulse = FactorInverse * VelocityChange;  // Delta Impulse = (J.M^(-1).J^(T))^(-1).(ContactVelocityError)

			if (bInApplyRelaxation && Chaos_Collision_RelaxationEnabled)
			{
				Impulse *= CalculateRelaxationFactor(IterationParameters);
			}

			FReal RelativeScale;// Use this as a scale to avoid absolute distances
			if (bIsRigidDynamic0 && bIsRigidDynamic1)
			{
				RelativeScale = FMath::Min(VectorToPoint1.SizeSquared(), VectorToPoint2.SizeSquared());
			}
			else
			{
				RelativeScale = bIsRigidDynamic0 ? VectorToPoint1.SizeSquared() : VectorToPoint2.SizeSquared();
			}

		
			bool bUseAccumalatedImpulseInSolve = Contact.bUseAccumalatedImpulseInSolve &&
				((Contact.ContactMoveSQRDistance == (FReal)0) || (RelativeScale > (FReal)0 && Contact.ContactMoveSQRDistance / RelativeScale < Chaos_Collision_ContactMovementAllowance* Chaos_Collision_ContactMovementAllowance));
			Contact.bUseAccumalatedImpulseInSolve = bUseAccumalatedImpulseInSolve; // Once disabled, disabled until the end of the frame

			// clip the impulse so that the accumulated impulse is not in the wrong direction and in the friction cone
			// Clipping the accumulated impulse instead of the delta impulse (for the current iteration) is very important for jitter
			// Use a zero vector for the accumulated impulse if we are not allowed to use it (when the contact moves significantly between calls).
			const FVec3 AccImp = Contact.bUseAccumalatedImpulseInSolve ? AccumulatedImpulse : FVec3((FReal)0);
			const FVec3 NewUnclippedAccumulatedImpulse = AccImp + Impulse;
			FVec3 ClippedAccumulatedImpulse(0.0f);  // To be calculated
			{
				// Project to normal
				const FReal NewAccImpNormalSize = FVec3::DotProduct(NewUnclippedAccumulatedImpulse, Contact.Normal);

				// Clipping impulses to be positive and contacts to be penetrating or touching
				// non zero PenetrationBuffer avoids a small amount of jitter added by the ApplypushOut function.
				const FReal PenetrationBuffer = Chaos_Collision_CollisionClipTolerance;
				const bool bProcessContact = ((NewAccImpNormalSize > 0) && (Contact.Phi <= PenetrationBuffer));// || !AccImp.IsNearlyZero();

				if (bProcessContact)
				{					
					const FVec3 ImpulseTangential = NewUnclippedAccumulatedImpulse - NewAccImpNormalSize * Contact.Normal;
					const FReal ImpulseTangentialSize = ImpulseTangential.Size();
					const FReal MaximumImpulseTangential = Friction * NewAccImpNormalSize;
					if (ImpulseTangentialSize <= MaximumImpulseTangential)
					{
						//Static friction case.
						if (AngularFriction)
						{
							ApplyAngularFriction(
								Impulse, // In and out
								AngularImpulse, // In and out
								AngularFriction,
								Particle0->W() - Particle1->W(),
								VelocityChange,
								Contact.Normal,
								VectorToPoint1,
								VectorToPoint2,
								PBDRigid0,
								PBDRigid1,
								bInfiniteMass0,
								bInfiniteMass1,
								Q0,
								Q1,
								WorldSpaceInvI1,
								WorldSpaceInvI2);
						}
						ClippedAccumulatedImpulse = Impulse + AccImp;
					}
					else
					{
						// Projecting the impulse, like in the following commented out line, is a simplification that fails with fast sliding contacts:
						// Because: The Factor matrix will change the direction of the vectors
						// ClippedAccumulatedImpulse = (MaximumImpulseTangential / ImpulseTangentialSize) * ImpulseTangential + NewAccImpNormalSize * Contact.Normal;

						//outside friction cone, solve for normal relative velocity and keep tangent at cone edge
						FVec3 Tangent = ImpulseTangential.GetSafeNormal();
						FVec3 DirectionalFactor = Factor * (Contact.Normal + Friction * Tangent);
						FReal ImpulseDenominator = FVec3::DotProduct(Contact.Normal, DirectionalFactor);
						if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Contact:%s\n\nParticle:%s\n\nLevelset:%s\n\nDirectionalFactor:%s, ImpulseDenominator:%f"),
							*Contact.ToString(),
							*Particle0->ToString(),
							*Particle1->ToString(),
							*DirectionalFactor.ToString(), ImpulseDenominator))
						{
							ImpulseDenominator = (FReal)1;
						}
						FReal RelativeNormalVelocity = FVec3::DotProduct(VelocityChange, Contact.Normal);
						const FReal ImpulseMag = RelativeNormalVelocity / ImpulseDenominator;
						ClippedAccumulatedImpulse = ImpulseMag * (Contact.Normal + Friction * Tangent) + AccImp;
						// Clip to zero
						if (FVec3::DotProduct(ClippedAccumulatedImpulse, Contact.Normal) <= (FReal)0)
						{
							ClippedAccumulatedImpulse = FVec3(0);
						}
					}
				}
				else
				{
					// Clipped Impulse is 0
					ClippedAccumulatedImpulse = FVec3(0); // Important case: the delta impulse should remove the accumulated impulse
				}
			}

			FVec3 OutDeltaImpulse = ClippedAccumulatedImpulse - AccImp;
			if (Chaos_Collision_EnergyClampEnabled != 0)
			{
				// Clamp the delta impulse to make sure we don't gain kinetic energy (ignore potential energy)
				// This should not modify the output impulses very often
				OutDeltaImpulse = GetEnergyClampedImpulse(Particle0->CastToRigidParticle(), Particle1->CastToRigidParticle(), OutDeltaImpulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
			}

			DV0 = FVec3(0);
			DW0 = FVec3(0);

			if (!bInfiniteMass0)
			{
				// Velocity update for next step
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint1, OutDeltaImpulse) + AngularImpulse;
				DV0 = InvM1 * OutDeltaImpulse;
				DW0 = WorldSpaceInvI1 * NetAngularImpulse;
			}

			DV1 = FVec3(0);
			DW1 = FVec3(0);

			if (!bInfiniteMass1)
			{
				// Velocity update for next step
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint2, -OutDeltaImpulse) - AngularImpulse;
				DV1 = -InvM2 * OutDeltaImpulse;
				DW1 = WorldSpaceInvI2 * NetAngularImpulse;
			}

			AccumulatedImpulse += OutDeltaImpulse; // Now update the accumulated Impulse (we need to keep track of this even if it is not used by the contact solver)
		}


		void ApplyContactAccImpulse(FCollisionContact& Contact,
			TGenericParticleHandle<FReal, 3> Particle0,
			TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			FVec3& AccumulatedImpulse)  // InOut
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			FVec3 DV0, DW0, DV1, DW1;
			CalculateContactVelocityCorrections(
				Contact, 
				Particle0, 
				Particle1, 
				IterationParameters, 
				ParticleParameters, 
				bIsRigidDynamic0,
				bIsRigidDynamic1,
				P0, Q0, P1, Q1,
				true, true, true, true, 
				AccumulatedImpulse, 
				DV0, DW0, DV1, DW1);


			if (bIsRigidDynamic0)
			{
				PBDRigid0->V() += DV0;
				PBDRigid0->W() += DW0;
				// Position update as part of pbd
				P0 += (DV0 * IterationParameters.Dt);
				Q0 += FRotation3::FromElements(DW0, 0.f) * Q0 * IterationParameters.Dt * FReal(0.5);
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(PBDRigid0, P0, Q0);
			}

			if (bIsRigidDynamic1)
			{
				PBDRigid1->V() += DV1;
				PBDRigid1->W() += DW1;
				// Position update as part of pbd
				P1 += (DV1 * IterationParameters.Dt);
				Q1 += FRotation3::FromElements(DW1, 0.f) * Q1 * IterationParameters.Dt * FReal(0.5);
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
			}
		}


		// Apply contacts, impulse clipping is done on delta impulses as opposed to Accumulated impulses
		FVec3 ApplyContact(FCollisionContact& Contact,
			TGenericParticleHandle<FReal, 3> Particle0, 
			TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters & IterationParameters,
			const FContactParticleParameters & ParticleParameters)
		{
			FVec3 AccumulatedImpulse(0);

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;
			const bool bOneWayInteraction0 = bIsRigidDynamic0 && PBDRigid0->OneWayInteraction();
			const bool bOneWayInteraction1 = bIsRigidDynamic1 && PBDRigid1->OneWayInteraction();
			const bool bInfiniteMass0 = !bIsRigidDynamic0 || (!bOneWayInteraction0 && bOneWayInteraction1);
			const bool bInfiniteMass1 = !bIsRigidDynamic1 || (bOneWayInteraction0 && !bOneWayInteraction1);

			const FVec3 ZeroVector = FVec3(0);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			const FVec3 VectorToPoint1 = Contact.Location - P0;
			const FVec3 VectorToPoint2 = Contact.Location - P1;
			const FVec3 Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, VectorToPoint1);
			const FVec3 Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, VectorToPoint2);
			const FVec3 RelativeVelocity = Body1Velocity - Body2Velocity;
			const FReal RelativeNormalVelocity = FVec3::DotProduct(RelativeVelocity, Contact.Normal);
			

			FReal RelativeNormalVelocityForRestitution = RelativeNormalVelocity; // Relative velocity in direction of the normal as used by restitution
			FReal RelativeVelocityForRestitutionSize = RelativeVelocity.Size();
			// Use the previous contact velocities to calculate the restitution response
			if (Chaos_Collision_PrevVelocityRestitutionEnabled && Contact.Restitution > (FReal)0.0f)
			{
				RelativeNormalVelocityForRestitution = CalculateRelativeNormalVelocityForRestitution(Particle0, Particle1, Q0, Q1, Contact.Normal, VectorToPoint1, VectorToPoint2, RelativeVelocityForRestitutionSize);
			}

			// Resting contact if very close to the surface
			bool bApplyRestitution = (RelativeVelocityForRestitutionSize > ParticleParameters.RestitutionVelocityThreshold);
			FReal Restitution = (bApplyRestitution) ? Contact.Restitution : (FReal)0;

			if (RelativeNormalVelocity < 0) // ignore separating constraints
			{
				*IterationParameters.NeedsAnotherIteration = true;

				FMatrix33 WorldSpaceInvI1 = !bInfiniteMass0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
				FMatrix33 WorldSpaceInvI2 = !bInfiniteMass1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
				FReal InvM1 = !bInfiniteMass0 ? PBDRigid0->InvM() : 0.f;
				FReal InvM2 = !bInfiniteMass1 ? PBDRigid1->InvM() : 0.f;
				FMatrix33 Factor =
				    (!bInfiniteMass0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, InvM1) : FMatrix33(0)) +
				    (!bInfiniteMass1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, InvM2) : FMatrix33(0));
				FVec3 Impulse;
				FVec3 AngularImpulse(0);
				
				FReal Friction = Contact.Friction;
				FReal AngularFriction = Contact.AngularFriction;

				if (Friction > 0)
				{
					FVec3 VelocityChange = -(Restitution * RelativeNormalVelocityForRestitution * Contact.Normal + RelativeVelocity);
					FReal NormalVelocityChange = FVec3::DotProduct(VelocityChange, Contact.Normal);
					FMatrix33 FactorInverse = Factor.Inverse();
					FVec3 MinimalImpulse = FactorInverse * VelocityChange;
					const FReal TangentialSize = (VelocityChange - NormalVelocityChange * Contact.Normal).Size();
					if (TangentialSize <= Friction * NormalVelocityChange)
					{
						//within friction cone so just solve for static friction stopping the object
						Impulse = MinimalImpulse;
						if (AngularFriction)
						{
							ApplyAngularFriction(
								Impulse, // In and out
								AngularImpulse, // In and out
								AngularFriction,
								Particle0->W() - Particle1->W(),
								VelocityChange,
								Contact.Normal,
								VectorToPoint1,
								VectorToPoint2,
								PBDRigid0,
								PBDRigid1,
								bInfiniteMass0,
								bInfiniteMass1,
								Q0,
								Q1,
								WorldSpaceInvI1,
								WorldSpaceInvI2);
						}
					}
					else
					{
						//outside friction cone, solve for normal relative velocity and keep tangent at cone edge
						FVec3 Tangent = (RelativeVelocity - FVec3::DotProduct(RelativeVelocity, Contact.Normal) * Contact.Normal).GetSafeNormal();
						FVec3 DirectionalFactor = Factor * (Contact.Normal - Friction * Tangent);
						FReal ImpulseDenominator = FVec3::DotProduct(Contact.Normal, DirectionalFactor);
						if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Contact:%s\n\nParticle:%s\n\nLevelset:%s\n\nDirectionalFactor:%s, ImpulseDenominator:%f"),
							*Contact.ToString(),
							*Particle0->ToString(),
							*Particle1->ToString(),
							*DirectionalFactor.ToString(), ImpulseDenominator))
						{
							ImpulseDenominator = (FReal)1;
						}

						const FReal ImpulseMag = -(RelativeNormalVelocity + Restitution * RelativeNormalVelocityForRestitution) / ImpulseDenominator;
						Impulse = ImpulseMag * (Contact.Normal - Friction * Tangent);
					}
				}
				else
				{
					FReal ImpulseDenominator = FVec3::DotProduct(Contact.Normal, Factor * Contact.Normal);
					FVec3 ImpulseNumerator = -(RelativeNormalVelocity + Restitution * RelativeNormalVelocityForRestitution) * Contact.Normal;
					if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Contact:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Constraint.Normal:%s, ImpulseDenominator:%f"),
						*Contact.ToString(),
						*Particle0->ToString(),
						*Particle1->ToString(),
						*(Factor * Contact.Normal).ToString(), ImpulseDenominator))
					{
						ImpulseDenominator = (FReal)1;
					}
					Impulse = ImpulseNumerator / ImpulseDenominator;
				}

				if (Chaos_Collision_EnergyClampEnabled != 0)
				{
					Impulse = GetEnergyClampedImpulse(Particle0->CastToRigidParticle(), Particle1->CastToRigidParticle(), Impulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
				}

				if (Chaos_Collision_RelaxationEnabled)
				{
					Impulse *= CalculateRelaxationFactor(IterationParameters);
				}
				
				AccumulatedImpulse += Impulse;

				if (bIsRigidDynamic0)
				{
					// Velocity update for next step
					FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint1, Impulse) + AngularImpulse;
					FVec3 DV = InvM1 * Impulse;
					FVec3 DW = WorldSpaceInvI1 * NetAngularImpulse;
					PBDRigid0->V() += DV;
					PBDRigid0->W() += DW;
					// Position update as part of pbd
					P0 += (DV * IterationParameters.Dt);
					Q0 += FRotation3::FromElements(DW, 0.f) * Q0 * IterationParameters.Dt * FReal(0.5);
					Q0.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid0, P0, Q0);
				}
				if (bIsRigidDynamic1)
				{
					// Velocity update for next step
					FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint2, -Impulse) - AngularImpulse;
					FVec3 DV = -InvM2 * Impulse;
					FVec3 DW = WorldSpaceInvI2 * NetAngularImpulse;
					PBDRigid1->V() += DV;
					PBDRigid1->W() += DW;
					// Position update as part of pbd
					P1 += (DV * IterationParameters.Dt);
					Q1 += FRotation3::FromElements(DW, 0.f) * Q1 * IterationParameters.Dt * FReal(0.5);
					Q1.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
				}
			}
			return AccumulatedImpulse;
		}

		// A PBD collision penetration correction.
		FVec3 ApplyContact2(FCollisionContact& Contact,
			TGenericParticleHandle<FReal, 3> Particle0,
			TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
			FVec3 AccumulatedImpulse(0);
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;
			const bool bOneWayInteraction0 = bIsRigidDynamic0 && PBDRigid0->OneWayInteraction();
			const bool bOneWayInteraction1 = bIsRigidDynamic1 && PBDRigid1->OneWayInteraction();
			const bool bInfiniteMass0 = !bIsRigidDynamic0 || (!bOneWayInteraction0 && bOneWayInteraction1);
			const bool bInfiniteMass1 = !bIsRigidDynamic1 || (bOneWayInteraction0 && !bOneWayInteraction1);

//#if INTEL_ISPC
//			if (bChaos_Collision_ISPC_Enabled)
//			{
//				FVec3 PActor0 = Particle0->P();
//				FRotation3 QActor0 = Particle0->Q();
//				FVec3 PActor1 = Particle1->P();
//				FRotation3 QActor1 = Particle1->Q();
//				const FReal InvM0 = Particle0->InvM();
//				const FVec3 InvI0 = Particle0->InvI().GetDiagonal();
//				const FVec3 PCoM0 = Particle0->CenterOfMass();
//				const FRotation3 QCoM0 = Particle0->RotationOfMass();
//				const FReal InvM1 = Particle1->InvM();
//				const FVec3 InvI1 = Particle1->InvI().GetDiagonal();
//				const FVec3 PCoM1 = Particle1->CenterOfMass();
//				const FRotation3 QCoM1 = Particle1->RotationOfMass();
//				ispc::ApplyContact2(
//					(ispc::FCollisionContact*) &Contact,
//					(ispc::FVector&)PActor0,
//					(ispc::FVector4&)QActor0,
//					(ispc::FVector&)PActor1,
//					(ispc::FVector4&)QActor1,
//					InvM0,
//					(const ispc::FVector&)InvI0,
//					(const ispc::FVector&)PCoM0,
//					(const ispc::FVector4&)QCoM0,
//					InvM1,
//					(const ispc::FVector&)InvI1,
//					(const ispc::FVector&)PCoM1,
//					(const ispc::FVector4&)QCoM1);
//
//				if (bIsRigidDynamic0)
//				{
//					PBDRigid0->SetP(PActor0);
//					PBDRigid0->SetQ(QActor0);
//				}
//				if (bIsRigidDynamic1)
//				{
//					PBDRigid1->SetP(PActor1);
//					PBDRigid1->SetQ(QActor1);
//				}
//
//				*IterationParameters.NeedsAnotherIteration = true;
//				return AccumulatedImpulse;
//			}
//#endif

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
			FVec3 VectorToPoint0 = Contact.Location - P0;
			FVec3 VectorToPoint1 = Contact.Location - P1;

			if (Contact.Phi < 0)
			{
				*IterationParameters.NeedsAnotherIteration = true;

				bool bApplyResitution = (Contact.Restitution > 0.0f);
				bool bHaveRestitutionPadding = (Contact.RestitutionPadding > 0.0f);
				bool bApplyFriction = (Contact.Friction > 0) && (IterationParameters.Dt > SMALL_NUMBER);

				// If we have restitution, padd the constraint by an amount that enforces the outgoing velocity constraint
				// Really this should be per contact point, not per constraint.
				// NOTE: once we have calculated a padding, it is locked in for the rest of the iterations, and automatically
				// included in the Phi we get back from collision detection. The first time we calculate it, we must also
				// add the padding to the Phi (since it was from pre-padded collision detection).
				if (bApplyResitution && !bHaveRestitutionPadding)
				{
					FVec3 V0 = Particle0->V();
					FVec3 W0 = Particle0->W();
					FVec3 V1 = Particle1->V();
					FVec3 W1 = Particle1->W();
					FVec3 CV0 = V0 + FVec3::CrossProduct(W0, VectorToPoint0);
					FVec3 CV1 = V1 + FVec3::CrossProduct(W1, VectorToPoint1);
					FVec3 CV = CV0 - CV1;
					FReal CVNormal = FVec3::DotProduct(CV, Contact.Normal);

					// No restitution below threshold normal velocity (CVNormal is negative here)
					if (CVNormal < -ParticleParameters.RestitutionVelocityThreshold)
					{
						Contact.RestitutionPadding = -(1.0f + Contact.Restitution) * CVNormal * IterationParameters.Dt + Contact.Phi;
						Contact.Phi -= Contact.RestitutionPadding;
					}
				}
			
				FReal InvM0 = !bInfiniteMass0 ? PBDRigid0->InvM() : 0.0f;
				FReal InvM1 = !bInfiniteMass1 ? PBDRigid1->InvM() : 0.0f;
				FMatrix33 InvI0 = !bInfiniteMass0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
				FMatrix33 InvI1 = !bInfiniteMass1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
				FMatrix33 ContactInvI =
				    (!bInfiniteMass0 ? ComputeFactorMatrix3(VectorToPoint0, InvI0, InvM0) : FMatrix33(0)) +
				    (!bInfiniteMass1 ? ComputeFactorMatrix3(VectorToPoint1, InvI1, InvM1) : FMatrix33(0));

				// Calculate the normal correction
				FVec3 NormalError = Contact.Phi * Contact.Normal;
				FReal NormalImpulseDenominator = FVec3::DotProduct(Contact.Normal, ContactInvI * Contact.Normal);
				FVec3 NormalImpulseNumerator = -NormalError;
				FVec3 NormalCorrection = NormalImpulseNumerator / NormalImpulseDenominator;

				// Calculate lateral correction, clamped to the friction cone. Kinda.
				FVec3 LateralCorrection = FVec3(0);
				if (bApplyFriction)
				{
					// @todo(ccaulfield): use initial velocity (as for restitution) and accumulate friction force per contact point
					FVec3 X0 = FParticleUtilitiesXR::GetCoMWorldPosition(Particle0);
					FVec3 X1 = FParticleUtilitiesXR::GetCoMWorldPosition(Particle1);
					FRotation3 R0 = FParticleUtilitiesXR::GetCoMWorldRotation(Particle0);
					FRotation3 R1 = FParticleUtilitiesXR::GetCoMWorldRotation(Particle1);
					FVec3 V0 = FVec3::CalculateVelocity(X0, P0, IterationParameters.Dt);
					FVec3 W0 = FRotation3::CalculateAngularVelocity(R0, Q0, IterationParameters.Dt);
					FVec3 V1 = FVec3::CalculateVelocity(X1, P1, IterationParameters.Dt);
					FVec3 W1 = FRotation3::CalculateAngularVelocity(R1, Q1, IterationParameters.Dt);
					FVec3 CV0 = V0 + FVec3::CrossProduct(W0, VectorToPoint0);
					FVec3 CV1 = V1 + FVec3::CrossProduct(W1, VectorToPoint1);
					FVec3 CV = CV0 - CV1;
					FReal CVNormal = FVec3::DotProduct(CV, Contact.Normal);
					if (CVNormal < 0.0f)
					{
						FVec3 CVLateral = CV - CVNormal * Contact.Normal;
						FReal CVLateralMag = CVLateral.Size();
						if (CVLateralMag > KINDA_SMALL_NUMBER)
						{
							FVec3 DirLateral = CVLateral / CVLateralMag;
							FVec3 LateralImpulseNumerator = -CVLateral * IterationParameters.Dt;
							FReal LateralImpulseDenominator = FVec3::DotProduct(DirLateral, ContactInvI * DirLateral);
							LateralCorrection = LateralImpulseNumerator / LateralImpulseDenominator;
							FReal LateralImpulseMag = LateralCorrection.Size();
							FReal NormalImpulseMag = NormalCorrection.Size();
							if (LateralImpulseMag > Contact.Friction * NormalImpulseMag)
							{
								LateralCorrection *= Contact.Friction * NormalImpulseMag / LateralImpulseMag;
							}
						}
					}
				}

				// Net Correction
				FVec3 DX = NormalCorrection + LateralCorrection;
				
				if (bIsRigidDynamic0)
				{
					FVec3 DP0 = InvM0 * DX;
					FVec3 DR0 = Utilities::Multiply(InvI0, FVec3::CrossProduct(VectorToPoint0, DX));
					P0 += DP0;
					Q0 += FRotation3::FromElements(DR0, 0.f) * Q0 * FReal(0.5);
					Q0.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid0, P0, Q0);
				}
				if (bIsRigidDynamic1)
				{
					FVec3 DP1 = InvM1 * -DX;
					FVec3 DR1 = Utilities::Multiply(InvI1, FVec3::CrossProduct(VectorToPoint1, -DX));
					P1 += DP1;
					Q1 += FRotation3::FromElements(DR1, 0.f) * Q1 * FReal(0.5);
					Q1.Normalize();
					FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
				}
			}
			return AccumulatedImpulse;
		}


		template<typename T_CONSTRAINT>
		void ApplyImpl(T_CONSTRAINT& Constraint, const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[1]);

			for (int32 PairIt = 0; PairIt < IterationParameters.NumPairIterations; ++PairIt)
			{
				// Update the contact information based on current particles' positions
				bool bRequiresCollisionUpdate = true;
				if (bRequiresCollisionUpdate)
				{
					Collisions::Update(Constraint, ParticleParameters.CullDistance, IterationParameters.Dt);
				}

				// Permanently disable a constraint that is beyond the cull distance
				if (Constraint.GetPhi() >= ParticleParameters.CullDistance)
				{
					if (ParticleParameters.bCanDisableContacts)
					{
						Constraint.SetDisabled(true);
					}
					return;
				}

				// Do not early out here in the case of Accumulated impulse solve
				// @todo(chaos): remove this early out when we settle on manifolds
				const bool bIsAccumulatingImpulses = Constraint.UseIncrementalManifold() || Chaos_Collision_UseAccumulatedImpulseClipSolve;
				if (Constraint.GetPhi() >= 0.0f && !bIsAccumulatingImpulses)
				{
					return;
				}

				// @todo(chaos): fix the collided flag - it will sometimes be set if clipping is enabled, even if there was no contact...
				if (ParticleParameters.Collided)
				{
					Particle0->AuxilaryValue(*ParticleParameters.Collided) = true;
					Particle1->AuxilaryValue(*ParticleParameters.Collided) = true;
				}

				// What Apply algorithm should we use? Controlled by the solver, with forcable cvar override for now...
				bool bUseVelocityMode = (IterationParameters.ApplyType == ECollisionApplyType::Velocity);
				if (Chaos_Collision_ForceApplyType != 0)
				{
					bUseVelocityMode = (Chaos_Collision_ForceApplyType == (int32)ECollisionApplyType::Velocity);
				}

				if (bUseVelocityMode)
				{
					if (Constraint.UseIncrementalManifold())
					{
						ApplyContactManifold(Constraint, Particle0, Particle1, IterationParameters, ParticleParameters);
					}
					else if (Chaos_Collision_UseAccumulatedImpulseClipSolve)
					{
						// This version of Apply contact is different from the original in that it clips accumulated impulses instead of delta impulses
						// Todo: we need multiple contact points per pair for this to be effective
						ApplyContactAccImpulse(Constraint.Manifold, Particle0, Particle1, IterationParameters, ParticleParameters, Constraint.AccumulatedImpulse);
					}
					else
					{
						Constraint.AccumulatedImpulse += ApplyContact(Constraint.Manifold, Particle0, Particle1, IterationParameters, ParticleParameters);
					}
				}
				else
				{
					Constraint.AccumulatedImpulse += ApplyContact2(Constraint.Manifold, Particle0, Particle1, IterationParameters, ParticleParameters);
				}
			}
		}
		
		void ApplySweptImpl(FRigidBodySweptPointContactConstraint& Constraint, const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			const FReal TimeOfImpactErrorMargin = 20.0f;  // Large error margin in cm, this is to ensure that 1) velocity is solved for at the time of impact, and  2) the contact is not disabled
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[1]);

			if (Constraint.bShouldTreatAsSinglePoint || IterationParameters.Iteration > 0 || Constraint.TimeOfImpact == 1)
			{
				// If not on first iteration, or at TOI = 1 (normal constraint) we don't want to split timestep at TOI.
				ApplyImpl(Constraint, IterationParameters, ParticleParameters);
				return;
			}

			// Rebuild iteration params with partial dt, and non-zero iteration count to force update of constraint.
			// P may have changed due to other constraints, so at TOI our manifold needs updating.
			const FReal PartialDT = Constraint.TimeOfImpact * IterationParameters.Dt;
			const FReal RemainingDT = (1 - Constraint.TimeOfImpact) * IterationParameters.Dt;
			const int32 FakeIteration = IterationParameters.NumIterations / 2; // For iteration count dependent effects (like relaxation)
			const int32 PartialPairIterations = FMath::Max(IterationParameters.NumPairIterations, 2); // Do at least 2 pair iterations
			const FContactIterationParameters IterationParametersPartialDT{ PartialDT, FakeIteration, IterationParameters.NumIterations, PartialPairIterations, IterationParameters.ApplyType, IterationParameters.NeedsAnotherIteration };
			const FContactIterationParameters IterationParametersRemainingDT{ RemainingDT, FakeIteration, IterationParameters.NumIterations, IterationParameters.NumPairIterations, IterationParameters.ApplyType, IterationParameters.NeedsAnotherIteration };
			const FContactParticleParameters CCDParticleParamaters{ ParticleParameters.CullDistance + TimeOfImpactErrorMargin, ParticleParameters.RestitutionVelocityThreshold, ParticleParameters.bCanDisableContacts, ParticleParameters.Collided };

			// Rewind P to TOI and Apply
			Particle0->P() = FMath::Lerp(Particle0->X(), Particle0->P(), Constraint.TimeOfImpact);
			ApplyImpl(Constraint, IterationParametersPartialDT, CCDParticleParamaters);

			// Advance P to end of frame from TOI, and Apply
			Particle0->P() = Particle0->P() + Particle0->V() * RemainingDT;
			ApplyImpl(Constraint, IterationParametersRemainingDT, CCDParticleParamaters);
		}


		void Apply(FCollisionConstraintBase& Constraint, const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			if (Constraint.GetType() == FCollisionConstraintBase::FType::SinglePoint)
			{
				ApplyImpl(*Constraint.As<FRigidBodyPointContactConstraint>(), IterationParameters, ParticleParameters);
			}
			else if (Constraint.GetType() == FCollisionConstraintBase::FType::SinglePointSwept)
			{
				ApplySweptImpl(*Constraint.As<FRigidBodySweptPointContactConstraint>(), IterationParameters, ParticleParameters);
			}
			else if (Constraint.GetType() == FCollisionConstraintBase::FType::MultiPoint)
			{
				ApplyImpl(*Constraint.As<FRigidBodyMultiPointContactConstraint>(), IterationParameters, ParticleParameters);
			}
		}

		void ApplySinglePoint(FRigidBodyPointContactConstraint& Constraint, const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			ApplyImpl<FRigidBodyPointContactConstraint>(Constraint, IterationParameters, ParticleParameters);
		}

		void ApplyMultiPoint(FRigidBodyMultiPointContactConstraint& Constraint, const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			ApplyImpl<FRigidBodyMultiPointContactConstraint>(Constraint, IterationParameters, ParticleParameters);
		}

		// More jitter resistant version of ApplyPushOutContact
		void ApplyPushOutContactAccImpulse(
			FCollisionContact& Contact,
			TGenericParticleHandle<FReal, 3> Particle0, 
			TGenericParticleHandle<FReal, 3> Particle1,
			const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters,
			FVec3& AccumulatedImpulse)
		{
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			const bool bOneWayInteraction0 = bIsRigidDynamic0 && PBDRigid0->OneWayInteraction();
			const bool bOneWayInteraction1 = bIsRigidDynamic1 && PBDRigid1->OneWayInteraction();
			const bool bInfiniteMass0 = !bIsRigidDynamic0 || (!bOneWayInteraction0 && bOneWayInteraction1);
			const bool bInfiniteMass1 = !bIsRigidDynamic1 || (bOneWayInteraction0 && !bOneWayInteraction1);

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

			if ((bInfiniteMass0 || IsTemporarilyStatic0) && (bInfiniteMass1 || IsTemporarilyStatic1))
			{
				return;
			}

			const FVec3 ZeroVector = FVec3(0);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			// Solve the contact velocity without updating positions
			// Todo: Accuracy Investigation: Move this to the end of the function after updating positions (to solve velocities at the new locations instead), 
			// but also update the contact location if this is done
			{
				FVec3 DV0, DW0, DV1, DW1;
				CalculateContactVelocityCorrections(
					Contact,
					Particle0,
					Particle1,
					IterationParameters,
					ParticleParameters,
					bIsRigidDynamic0,
					bIsRigidDynamic1,
					P0, Q0, P1, Q1,
					false, false, false, false,
					AccumulatedImpulse,
					DV0, DW0, DV1, DW1);

				if (!bInfiniteMass0)
				{
					PBDRigid0->V() += DV0;
					PBDRigid0->W() += DW0;
				}
				if (!bInfiniteMass1)
				{
					PBDRigid1->V() += DV1;
					PBDRigid1->W() += DW1;
				}
			}

			FMatrix33 WorldSpaceInvI1 = !bInfiniteMass0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			FMatrix33 WorldSpaceInvI2 = !bInfiniteMass1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			
			FVec3 VectorToPoint1 = Contact.Location - P0;
			FVec3 VectorToPoint2 = Contact.Location - P1;
			FMatrix33 Factor =
				(!bInfiniteMass0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : FMatrix33(0)) +
				(!bInfiniteMass1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : FMatrix33(0));
			
			const FVec3  Error = -Contact.Phi * Contact.Normal;
			const FVec3 DeltaImpulseDt = FMatrix33(Factor.Inverse()) * Error;
			const FVec3 UnclippedImpulseDt = DeltaImpulseDt + (Contact.bUseAccumalatedImpulseInSolve ? AccumulatedImpulse * IterationParameters.Dt : FVec3(0));

			FVec3 ClippedImpulseDt = FVec3::DotProduct(UnclippedImpulseDt, Contact.Normal) > 0 ? UnclippedImpulseDt : FVec3(0);
			ClippedImpulseDt = FVec3::DotProduct(ClippedImpulseDt, Contact.Normal) * Contact.Normal;

			const FVec3 ClippedDeltaImpulseDt = ClippedImpulseDt - (Contact.bUseAccumalatedImpulseInSolve ? AccumulatedImpulse * IterationParameters.Dt : FVec3(0));
			AccumulatedImpulse = AccumulatedImpulse + ClippedDeltaImpulseDt / IterationParameters.Dt;

			FVec3 AngularImpulse1 = FVec3::CrossProduct(VectorToPoint1, ClippedDeltaImpulseDt);
			FVec3 AngularImpulse2 = FVec3::CrossProduct(VectorToPoint2, -ClippedDeltaImpulseDt);
			if (!IsTemporarilyStatic0 && !bInfiniteMass0)
			{
				const FVec3 Correction = PBDRigid0->InvM() * ClippedDeltaImpulseDt;
				P0 += Correction;
				Q0 = FRotation3::FromVector(WorldSpaceInvI1 * AngularImpulse1) * Q0;
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
			}
			if (!IsTemporarilyStatic1 && !bInfiniteMass1)
			{
				P1 -= PBDRigid1->InvM() * ClippedDeltaImpulseDt;
				Q1 = FRotation3::FromVector(WorldSpaceInvI2 * AngularImpulse2) * Q1;
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
			}
		}

		FVec3 ApplyPushOutContact(
			FCollisionContact& Contact,
			TGenericParticleHandle<FReal, 3> Particle0, 
			TGenericParticleHandle<FReal, 3> Particle1,
			const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
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

			FVec3 AccumulatedImpulse(0);

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && (PBDRigid0->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic0;
			const bool bIsRigidDynamic1 = PBDRigid1 && (PBDRigid1->ObjectState() == EObjectStateType::Dynamic) && !IsTemporarilyStatic1;
			const bool bOneWayInteraction0 = bIsRigidDynamic0 && PBDRigid0->OneWayInteraction();
			const bool bOneWayInteraction1 = bIsRigidDynamic1 && PBDRigid1->OneWayInteraction();
			const bool bInfiniteMass0 = !bIsRigidDynamic0 || (!bOneWayInteraction0 && bOneWayInteraction1);
			const bool bInfiniteMass1 = !bIsRigidDynamic1 || (bOneWayInteraction0 && !bOneWayInteraction1);

			const FVec3 ZeroVector = FVec3(0);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			if (Contact.Phi >= 0.0f)
			{
				return AccumulatedImpulse;
			}

			if (!bIsRigidDynamic0 && !bIsRigidDynamic1)
			{
				return AccumulatedImpulse;
			}

			FMatrix33 WorldSpaceInvI1 = !bInfiniteMass0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			FMatrix33 WorldSpaceInvI2 = !bInfiniteMass1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			FReal InvM1 = !bInfiniteMass0 ? PBDRigid0->InvM() : 0.f;
			FReal InvM2 = !bInfiniteMass1 ? PBDRigid1->InvM() : 0.f;
			FVec3 VectorToPoint1 = Contact.Location - P0;
			FVec3 VectorToPoint2 = Contact.Location - P1;
			FMatrix33 Factor =
			    (!bInfiniteMass0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, InvM1) : FMatrix33(0)) +
			    (!bInfiniteMass1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, InvM2) : FMatrix33(0));
			FReal Numerator = FMath::Min((FReal)(IterationParameters.Iteration + 2), (FReal)IterationParameters.NumIterations);
			FReal ScalingFactor = Numerator / (FReal)IterationParameters.NumIterations;

			//if pushout is needed we better fix relative velocity along normal. Treat it as if 0 restitution
			FVec3 Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, VectorToPoint1);
			FVec3 Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, VectorToPoint2);
			FVec3 RelativeVelocity = Body1Velocity - Body2Velocity;
			const FReal RelativeVelocityDotNormal = FVec3::DotProduct(RelativeVelocity, Contact.Normal);
			if (RelativeVelocityDotNormal < 0)
			{
				*IterationParameters.NeedsAnotherIteration = true;
			
				const FVec3 ImpulseNumerator = -FVec3::DotProduct(RelativeVelocity, Contact.Normal) * Contact.Normal * ScalingFactor;
				const FVec3 FactorContactNormal = Factor * Contact.Normal;
				FReal ImpulseDenominator = FVec3::DotProduct(Contact.Normal, FactorContactNormal);
				if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, 
					TEXT("ApplyPushout Contact:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Contact.Normal:%s, ImpulseDenominator:%f"),
					*Contact.ToString(),
					*Particle0->ToString(),
					*Particle1->ToString(),
					*FactorContactNormal.ToString(), ImpulseDenominator))
				{
					ImpulseDenominator = (FReal)1;
				}

				FVec3 VelocityFixImpulse = ImpulseNumerator / ImpulseDenominator;
				if (Chaos_Collision_EnergyClampEnabled)
				{
					VelocityFixImpulse = GetEnergyClampedImpulse(Particle0->CastToRigidParticle(), Particle1->CastToRigidParticle(), VelocityFixImpulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
				}
				AccumulatedImpulse += VelocityFixImpulse;	//question: should we track this?
				if (bIsRigidDynamic0)
				{
					FVec3 AngularImpulse = FVec3::CrossProduct(VectorToPoint1, VelocityFixImpulse);
					PBDRigid0->V() += InvM1 * VelocityFixImpulse;
					PBDRigid0->W() += WorldSpaceInvI1 * AngularImpulse;

				}

				if (bIsRigidDynamic1)
				{
					FVec3 AngularImpulse = FVec3::CrossProduct(VectorToPoint2, -VelocityFixImpulse);
					PBDRigid1->V() -= InvM2 * VelocityFixImpulse;
					PBDRigid1->W() += WorldSpaceInvI2 * AngularImpulse;
				}

			}


			FVec3 Impulse = FMatrix33(Factor.Inverse()) * (-Contact.Phi * ScalingFactor * Contact.Normal);
			FVec3 AngularImpulse1 = FVec3::CrossProduct(VectorToPoint1, Impulse);
			FVec3 AngularImpulse2 = FVec3::CrossProduct(VectorToPoint2, -Impulse);
			if (bIsRigidDynamic0)
			{
				P0 += InvM1 * Impulse;
				Q0 = FRotation3::FromVector(WorldSpaceInvI1 * AngularImpulse1) * Q0;
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
			}
			if (bIsRigidDynamic1)
			{
				P1 -= InvM2 * Impulse;
				Q1 = FRotation3::FromVector(WorldSpaceInvI2 * AngularImpulse2) * Q1;
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
			}

			return AccumulatedImpulse;
		}


		template<typename T_CONSTRAINT>
		void ApplyPushOutImpl(T_CONSTRAINT& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters, const FVec3& GravityDir)
		{
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[1]);

			for (int32 PairIt = 0; PairIt < IterationParameters.NumPairIterations; ++PairIt)
			{
				bool bRequiresCollisionUpdate = true;
				if (bRequiresCollisionUpdate)
				{
					Update(Constraint, ParticleParameters.CullDistance, IterationParameters.Dt);
				}

				// Permanently disable a constraint that is beyond the cull distance
				if (Constraint.GetPhi() >= ParticleParameters.CullDistance)
				{
					if (ParticleParameters.bCanDisableContacts)
					{
						Constraint.SetDisabled(true);
					}
					return;
				}

				// Note: Cannot early-out here if using impulse clipping (which supports negative incremental corrections as long as net correction is positive)
				// @todo(chaos): remove this early out when we settle on manifolds
				const bool bIsAccumulatingImpulses = Constraint.UseIncrementalManifold() || (Chaos_Collision_UseAccumulatedImpulseClipSolve != 0);
				if (Constraint.GetPhi() >= 0.0f && !bIsAccumulatingImpulses)
				{
					return;
				}

				if (Constraint.UseIncrementalManifold())
				{
					if (FRigidBodyPointContactConstraint* PointConstraint = Constraint.template As<FRigidBodyPointContactConstraint>())
					{
						ApplyPushOutManifold(*PointConstraint, IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
					}
				}
				else if (Chaos_Collision_UseAccumulatedImpulseClipSolve != 0)
				{
					ApplyPushOutContactAccImpulse(Constraint.Manifold, Particle0, Particle1, IsTemporarilyStatic, IterationParameters, ParticleParameters, Constraint.AccumulatedImpulse);
				}
				else
				{
					Constraint.AccumulatedImpulse +=
						ApplyPushOutContact(Constraint.Manifold, Particle0, Particle1, IsTemporarilyStatic, IterationParameters, ParticleParameters);
				}
			}
		}

		void ApplyPushOut(FCollisionConstraintBase& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic, 
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters, const FVec3& GravityDir)
		{
			if (Constraint.GetType() == FCollisionConstraintBase::FType::SinglePoint)
			{
				ApplyPushOutImpl<FRigidBodyPointContactConstraint>(*Constraint.As<FRigidBodyPointContactConstraint>(), IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
			}
			else if (Constraint.GetType() == FCollisionConstraintBase::FType::SinglePointSwept)
			{
				ApplyPushOutImpl(*Constraint.As<FRigidBodySweptPointContactConstraint>(), IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
			}
			else if (Constraint.GetType() == FCollisionConstraintBase::FType::MultiPoint)
			{
				ApplyPushOutImpl<FRigidBodyMultiPointContactConstraint>(*Constraint.As<FRigidBodyMultiPointContactConstraint>(), IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
			}
		}

		void ApplyPushOutSinglePoint(FRigidBodyPointContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters, const FVec3& GravityDir)
		{
			ApplyPushOutImpl<FRigidBodyPointContactConstraint>(Constraint, IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
		}

		void ApplyPushOutMultiPoint(FRigidBodyMultiPointContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters, const FVec3& GravityDir)
		{
			ApplyPushOutImpl<FRigidBodyMultiPointContactConstraint>(Constraint, IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
		}

	} // Collisions

}// Chaos

