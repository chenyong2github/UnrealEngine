// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/Collision/CollisionSolver.h"
#include "Chaos/Collision/PBDCollisionSolver.h"
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

		FRealSingle Chaos_Collision_ContactMovementAllowance = 0.05f;
		FAutoConsoleVariableRef CVarChaosCollisionContactMovementAllowance(TEXT("p.Chaos.Collision.AntiJitterContactMovementAllowance"), Chaos_Collision_ContactMovementAllowance, 
			TEXT("If a contact is close to where it was during a previous iteration, we will assume it is the same contact that moved (to reduce jitter). Expressed as the fraction of movement distance and Centre of Mass distance to the contact point"));

		int32 Chaos_Collision_UseAccumulatedImpulseClipSolve = 0; // This requires multiple contact points per iteration per pair and contact points that don't move too much (in body space) to have an effect
		FAutoConsoleVariableRef CVarChaosCollisionImpulseClipSolve(TEXT("p.Chaos.Collision.UseAccumulatedImpulseClipSolve"), Chaos_Collision_UseAccumulatedImpulseClipSolve, TEXT("Use experimental Accumulated impulse clipped contact solve"));

		int32 Chaos_Collision_UseShockPropagation = 1;
		FAutoConsoleVariableRef CVarChaosCollisionUseShockPropagation(TEXT("p.Chaos.Collision.UseShockPropagation"), Chaos_Collision_UseShockPropagation, TEXT(""));

		FReal Chaos_Collision_CollisionClipTolerance = 0.01f;
		FAutoConsoleVariableRef CVarChaosCollisionClipTolerance(TEXT("p.Chaos.Collision.ClipTolerance"), Chaos_Collision_CollisionClipTolerance, TEXT(""));

		bool Chaos_Collision_CheckManifoldComplete = false;
		FAutoConsoleVariableRef CVarChaosCollisionCheckManifoldComplete(TEXT("p.Chaos.Collision.CheckManifoldComplete"), Chaos_Collision_CheckManifoldComplete, TEXT(""));

		void Update(FRigidBodyPointContactConstraint& Constraint, const FReal Dt)
		{
			const FRigidTransform3 Transform0 = GetTransform(Constraint.Particle[0]);
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);

			Constraint.ResetPhi(Constraint.GetCullDistance());
			UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest, FRigidBodyPointContactConstraint>(Constraint, Transform0, Transform1, Dt);
		}

		void UpdateSwept(FRigidBodySweptPointContactConstraint& Constraint, const FReal Dt)
		{
			FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.Particle[0]);
			// Note: This is unusual but we are using a mix of the previous and current transform
			// This is due to how CCD only rewinds the position
			const FRigidTransform3 TransformXQ0(Particle0->X(), Particle0->Q());
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);
			Constraint.ResetPhi(Constraint.GetCullDistance());
			
			// Update as a point constraint (base class).
			UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest, FRigidBodySweptPointContactConstraint>(Constraint, TransformXQ0, Transform1, Dt);
		}


		// A PBD collision penetration correction.
		// Currently only used by RBAN
		FVec3 ApplyContact2(FCollisionContact& Contact,
			FGenericParticleHandle Particle0,
			FGenericParticleHandle Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
			FVec3 AccumulatedImpulse(0);
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

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
			
				FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : 0.0f;
				FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : 0.0f;
				FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
				FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
				FMatrix33 ContactInvI =
					(bIsRigidDynamic0 ? ComputeFactorMatrix3(VectorToPoint0, InvI0, InvM0) : FMatrix33(0)) +
					(bIsRigidDynamic1 ? ComputeFactorMatrix3(VectorToPoint1, InvI1, InvM1) : FMatrix33(0));

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
			FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.Particle[0]);
			FGenericParticleHandle Particle1 = FGenericParticleHandle(Constraint.Particle[1]);

			for (int32 PairIt = 0; PairIt < IterationParameters.NumPairIterations; ++PairIt)
			{
				// Update the contact information based on current particles' positions
				bool bRequiresCollisionUpdate = true;
				if (bRequiresCollisionUpdate)
				{
					Collisions::Update(Constraint, IterationParameters.Dt);
				}

				// Permanently disable a constraint that is beyond the cull distance
				if (Constraint.GetPhi() >= Constraint.GetCullDistance())
				{
					if (ParticleParameters.bCanDisableContacts)
					{
						Constraint.SetDisabled(true);
					}
					return;
				}

				// Do not early out here in the case of Accumulated impulse solve
				// @todo(chaos): remove this early out when we settle on manifolds
				const bool bIsAccumulatingImpulses = Constraint.GetUseManifold() || Chaos_Collision_UseAccumulatedImpulseClipSolve;
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

				// What solver algorithm should we use?
				switch (IterationParameters.SolverType)
				{
				case EConstraintSolverType::GbfPbd:
					{
						ApplyContactManifold(Constraint, Particle0, Particle1, IterationParameters, ParticleParameters);
					}
					break;
				case EConstraintSolverType::StandardPbd:
					{
						Constraint.AccumulatedImpulse += ApplyContact2(Constraint.Manifold, Particle0, Particle1, IterationParameters, ParticleParameters);
					}
					break;
				case EConstraintSolverType::QuasiPbd:
					{
						FPBDCollisionSolver CollisionSolver;
						CollisionSolver.SolvePosition(Constraint, IterationParameters, ParticleParameters);
					}
					break;
				default:
					break;
				}
			}
		}
		
		void ApplySweptImpl(FRigidBodySweptPointContactConstraint& Constraint, const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.Particle[0]);
			FGenericParticleHandle Particle1 = FGenericParticleHandle(Constraint.Particle[1]);

			Collisions::UpdateSwept(Constraint, IterationParameters.Dt);
			
			const FContactParticleParameters CCDParticleParamaters{ ParticleParameters.RestitutionVelocityThreshold, true, ParticleParameters.Collided };
			if (Constraint.TimeOfImpact == 1)
			{
				// If TOI = 1 (normal constraint) we don't want to split timestep at TOI.
				ApplyImpl(Constraint, IterationParameters, CCDParticleParamaters);
				return;
			}

			// Rebuild iteration params with partial dt, and non-zero iteration count to force update of constraint.
			// P may have changed due to other constraints, so at TOI our manifold needs updating.
			const FReal PartialDT = Constraint.TimeOfImpact * IterationParameters.Dt;
			const FReal RemainingDT = (1 - Constraint.TimeOfImpact) * IterationParameters.Dt;
			const int32 FakeIteration = IterationParameters.NumIterations / 2; // For iteration count dependent effects (like relaxation) // @todo: Do we still need this?
			const int32 PartialPairIterations = FMath::Max(IterationParameters.NumPairIterations, 2); // Do at least 2 pair iterations // @todo: Do we still need this?
			const FContactIterationParameters IterationParametersPartialDT{ PartialDT, FakeIteration, IterationParameters.NumIterations, PartialPairIterations, IterationParameters.SolverType, IterationParameters.NeedsAnotherIteration };
			const FContactIterationParameters IterationParametersRemainingDT{ RemainingDT, FakeIteration, IterationParameters.NumIterations, IterationParameters.NumPairIterations, IterationParameters.SolverType, IterationParameters.NeedsAnotherIteration };

			// Rewind P to TOI and Apply
			Particle0->P() = FMath::Lerp(Particle0->X(), Particle0->P(), Constraint.TimeOfImpact);
			ApplyImpl(Constraint, IterationParametersPartialDT, CCDParticleParamaters);

			// Advance P to end of frame from TOI, and Apply
			if (IterationParameters.Iteration + 1 < IterationParameters.NumIterations)
			{
				Particle0->P() = Particle0->P() + Particle0->V() * RemainingDT; // If we are tunneling through something else due to this, it will be resolved in the next iteration
				ApplyImpl(Constraint, IterationParametersRemainingDT, CCDParticleParamaters);
			}
			else
			{
				// We get here if we cannot solve CCD collisions with the given number of iterations and restitution settings.
				// So don't do the remaining dt update. This will bleed the energy! (also: Ignore rotation)
				// To prevent this condition: increase number of iterations and/or reduce restitution and/or reduce velocities
				if (IterationParameters.Dt > SMALL_NUMBER)
				{
					// Update velocity to be consistent with PBD
					Particle0->SetV(1 / IterationParameters.Dt * (Particle0->P() - Particle0->X()));
				}
				else
				{
					Particle0->SetV(FVec3(0));
				}
			}
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
		}


		template<typename T_CONSTRAINT>
		void ApplyPushOutImpl(T_CONSTRAINT& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters, const FVec3& GravityDir)
		{
			FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.Particle[0]);
			FGenericParticleHandle Particle1 = FGenericParticleHandle(Constraint.Particle[1]);

			for (int32 PairIt = 0; PairIt < IterationParameters.NumPairIterations; ++PairIt)
			{
				Update(Constraint, IterationParameters.Dt);

				// Ignore contacts where the closest point is greater than cull distance
				if (Constraint.GetPhi() >= Constraint.GetCullDistance())
				{
					// Optionally permanently disable the contact for the remaining iterations
					if (ParticleParameters.bCanDisableContacts)
					{
						Constraint.SetDisabled(true);
					}
					return;
				}

				if (FRigidBodyPointContactConstraint* PointConstraint = Constraint.template As<FRigidBodyPointContactConstraint>())
				{
					switch (IterationParameters.SolverType)
					{
					case EConstraintSolverType::GbfPbd:
						{
							ApplyPushOutManifold(*PointConstraint, IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
						}
						break;
					case EConstraintSolverType::StandardPbd:
						{
							// Shouldn't have PushOut for Standard Pbd, but this is for experimentation (Collision PushOut Iterations should normally be set to 0 instead)
							ApplyPushOutManifold(*PointConstraint, IsTemporarilyStatic, IterationParameters, ParticleParameters, GravityDir);
						}
						break;
					case EConstraintSolverType::QuasiPbd:
						{
							FPBDCollisionSolver CollisionSolver;
							CollisionSolver.SolveVelocity(Constraint, IterationParameters, ParticleParameters);
						}
						break;
					default:
						break;
					}
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
		}

	} // Collisions

}// Chaos

