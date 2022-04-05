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

/**
 * @brief This file contains collision solver code for the StandardPbd solver (which will be removed at some point)
 * @see PBDCollisionSolver.cpp for the new solver
*/

namespace Chaos
{
	namespace Collisions
	{
		void Update(FPBDCollisionConstraint& Constraint, const FReal Dt)
		{
			// NOTE: These are actor transforms, not CoM transforms
			// \todo(chaos): see if we can easily switch to CoM transforms now in collision loop (shapes are held in actor space)
			const FSolverBody& Body0 = *Constraint.GetSolverBody0();
			const FSolverBody& Body1 = *Constraint.GetSolverBody1();
			const FRigidTransform3 Transform0 = FRigidTransform3(Body0.ActorP(), Body0.ActorQ());
			const FRigidTransform3 Transform1 = FRigidTransform3(Body1.ActorP(), Body1.ActorQ());

			Constraint.ResetPhi(Constraint.GetCullDistance());
			UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest>(Constraint, Transform0, Transform1, Dt);
		}

		// A PBD collision penetration correction.
		// Currently only used by RBAN
		FVec3 ApplyContact2(FPBDCollisionConstraint& Constraint,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters)
		{
			FSolverBody& Body0 = *Constraint.GetSolverBody0();
			FSolverBody& Body1 = *Constraint.GetSolverBody1();

			const FVec3 ContactLocation = Constraint.CalculateWorldContactLocation();
			const FVec3 ContactNormal = Constraint.CalculateWorldContactNormal();
			const FReal ContactFriction = Constraint.GetDynamicFriction();
			const FReal ContactRestitution = Constraint.GetRestitution();
			FReal ContactPhi = Constraint.GetPhi();
			FReal ContactRestitutionPadding = Constraint.GetRestitutionPadding();

			FVec3 VectorToPoint0 = ContactLocation - Body0.P();
			FVec3 VectorToPoint1 = ContactLocation - Body1.P();

			if (ContactPhi < 0)
			{
				*IterationParameters.NeedsAnotherIteration = true;

				bool bApplyResitution = (ContactRestitution > 0.0f);
				bool bHaveRestitutionPadding = (ContactRestitutionPadding > 0.0f);
				bool bApplyFriction = (ContactFriction > 0) && (IterationParameters.Dt > UE_SMALL_NUMBER);

				// If we have restitution, pad the constraint by an amount that enforces the outgoing velocity constraint
				// Really this should be per contact point, not per constraint.
				// NOTE: once we have calculated a padding, it is locked in for the rest of the iterations, and automatically
				// included in the Phi we get back from collision detection. The first time we calculate it, we must also
				// add the padding to the Phi (since it was from pre-padded collision detection).
				if (bApplyResitution && !bHaveRestitutionPadding)
				{
					const FVec3 BodyV0 = Body0.V() + (FVec3(Body0.DP()) / IterationParameters.Dt);
					const FVec3 BodyW0 = Body0.W() + (FVec3(Body0.DQ()) / IterationParameters.Dt);
					const FVec3 BodyV1 = Body1.V() + (FVec3(Body1.DP()) / IterationParameters.Dt);
					const FVec3 BodyW1 = Body1.W() + (FVec3(Body1.DQ()) / IterationParameters.Dt);
					FVec3 CV0 = BodyV0 + FVec3::CrossProduct(BodyW0, VectorToPoint0);
					FVec3 CV1 = BodyV1 + FVec3::CrossProduct(BodyW1, VectorToPoint1);
					FVec3 CV = CV0 - CV1;
					FReal CVNormal = FVec3::DotProduct(CV, ContactNormal);

					// No restitution below threshold normal velocity (CVNormal is negative here)
					if (CVNormal < -ParticleParameters.RestitutionVelocityThreshold)
					{
						ContactRestitutionPadding = -(1.0f + ContactRestitution) * CVNormal * IterationParameters.Dt + ContactPhi;
						ContactPhi -= ContactRestitutionPadding;

						Constraint.SetRestitutionPadding(ContactRestitutionPadding);
					}
				}
			
				FMatrix33 ContactInvI =
					(Body0.IsDynamic() ? ComputeFactorMatrix3(VectorToPoint0, FMatrix33(Body0.InvI()), FReal(Body0.InvM())) : FMatrix33(0)) +
					(Body1.IsDynamic() ? ComputeFactorMatrix3(VectorToPoint1, FMatrix33(Body1.InvI()), FReal(Body1.InvM())) : FMatrix33(0));

				// Calculate the normal correction
				FVec3 NormalError = ContactPhi * ContactNormal;
				FReal NormalImpulseDenominator = FVec3::DotProduct(ContactNormal, ContactInvI * ContactNormal);
				FVec3 NormalImpulseNumerator = -NormalError;
				FVec3 NormalCorrection = NormalImpulseNumerator / NormalImpulseDenominator;

				// Calculate lateral correction, clamped to the friction cone. Kinda.
				FVec3 LateralCorrection = FVec3(0);
				if (bApplyFriction)
				{
					// @todo(ccaulfield): use initial velocity (as for restitution) and accumulate friction force per contact point
					FVec3 V0 = FVec3::CalculateVelocity(Body0.X(), Body0.CorrectedP(), IterationParameters.Dt);
					FVec3 W0 = FRotation3::CalculateAngularVelocity(Body0.R(), Body0.CorrectedQ(), IterationParameters.Dt);
					FVec3 V1 = FVec3::CalculateVelocity(Body1.X(), Body1.CorrectedP(), IterationParameters.Dt);
					FVec3 W1 = FRotation3::CalculateAngularVelocity(Body1.R(), Body1.CorrectedQ(), IterationParameters.Dt);
					FVec3 CV0 = V0 + FVec3::CrossProduct(W0, VectorToPoint0);
					FVec3 CV1 = V1 + FVec3::CrossProduct(W1, VectorToPoint1);
					FVec3 CV = CV0 - CV1;
					FReal CVNormal = FVec3::DotProduct(CV, ContactNormal);
					if (CVNormal < 0.0f)
					{
						FVec3 CVLateral = CV - CVNormal * ContactNormal;
						FReal CVLateralMag = CVLateral.Size();
						if (CVLateralMag > UE_KINDA_SMALL_NUMBER)
						{
							FVec3 DirLateral = CVLateral / CVLateralMag;
							FVec3 LateralImpulseNumerator = -CVLateral * IterationParameters.Dt;
							FReal LateralImpulseDenominator = FVec3::DotProduct(DirLateral, ContactInvI * DirLateral);
							LateralCorrection = LateralImpulseNumerator / LateralImpulseDenominator;
							FReal LateralImpulseMag = LateralCorrection.Size();
							FReal NormalImpulseMag = NormalCorrection.Size();
							if (LateralImpulseMag > ContactFriction * NormalImpulseMag)
							{
								LateralCorrection *= ContactFriction * NormalImpulseMag / LateralImpulseMag;
							}
						}
					}
				}

				// Net Correction
				FVec3 DX = NormalCorrection + LateralCorrection;
				
				if (Body0.IsDynamic())
				{
					FVec3 DP0 = Body0.InvM() * DX;
					FVec3 DR0 = Utilities::Multiply(FMatrix33(Body0.InvI()), FVec3::CrossProduct(VectorToPoint0, DX));
					Body0.ApplyTransformDelta(DP0, DR0);
					Body0.ApplyCorrections();
					Body0.UpdateRotationDependentState();
				}
				if (Body1.IsDynamic())
				{
					FVec3 DP1 = Body1.InvM() * -DX;
					FVec3 DR1 = Utilities::Multiply(FMatrix33(Body1.InvI()), FVec3::CrossProduct(VectorToPoint1, -DX));
					Body1.ApplyTransformDelta(DP1, DR1);
					Body1.ApplyCorrections();
					Body1.UpdateRotationDependentState();
				}

				return DX;
			}

			return FVec3(0);
		}


		void Apply(FPBDCollisionConstraint& Constraint, const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters)
		{
			check(IterationParameters.SolverType == EConstraintSolverType::StandardPbd);

			for (int32 PairIt = 0; PairIt < IterationParameters.NumPairIterations; ++PairIt)
			{
				// Update the contact information based on current particles' positions
				if (Constraint.GetUseIncrementalCollisionDetection() || (Constraint.GetManifoldPoints().Num() == 0))
				{
					Collisions::Update(Constraint, IterationParameters.Dt);
				}
				else
				{
					Constraint.UpdateManifoldContacts();
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
				const bool bIsAccumulatingImpulses = Constraint.GetUseManifold();
				if (Constraint.GetPhi() >= 0.0f && !bIsAccumulatingImpulses)
				{
					return;
				}

				// @todo(chaos): fix the collided flag - it will sometimes be set if clipping is enabled, even if there was no contact...
				// @todo(chaos): this doesn't seem to be being set or used...remove Collided and the Aux Collided array if so. If it is supposed 
				// to work, it should be set via the Scatter method of the SolverBody
				if (ParticleParameters.Collided)
				{
					FGenericParticleHandle Particle0 = FGenericParticleHandle(Constraint.GetParticle0());
					FGenericParticleHandle Particle1 = FGenericParticleHandle(Constraint.GetParticle1());
					Particle0->AuxilaryValue(*ParticleParameters.Collided) = true;
					Particle1->AuxilaryValue(*ParticleParameters.Collided) = true;
				}

				Constraint.AccumulatedImpulse += ApplyContact2(Constraint, IterationParameters, ParticleParameters);
			}
		}

	} // Collisions

}// Chaos

