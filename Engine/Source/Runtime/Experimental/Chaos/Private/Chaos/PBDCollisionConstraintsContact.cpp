// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraintsContact.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Defines.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

//PRAGMA_DISABLE_OPTIMIZATION

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

namespace Chaos
{
#if INTEL_ISPC
	extern bool bChaos_Collision_ISPC_Enabled;
#endif

	extern int32 Chaos_Collision_UseAccumulatedImpulseClipSolve;

	namespace Collisions
	{
		int32 Chaos_Collision_EnergyClampEnabled = 1;
		FAutoConsoleVariableRef CVarChaosCollisionEnergyClampEnabled(TEXT("p.Chaos.Collision.EnergyClampEnabled"), Chaos_Collision_EnergyClampEnabled, TEXT("Whether to use energy clamping in collision apply step"));

		int32 Chaos_Collision_ForceApplyType = 0;
		FAutoConsoleVariableRef CVarChaosCollisionAlternativeApply(TEXT("p.Chaos.Collision.ForceApplyType"), Chaos_Collision_ForceApplyType, TEXT("Force Apply step to use Velocity(1) or Position(2) modes"));

		extern void UpdateManifold(FRigidBodyMultiPointContactConstraint& Constraint, const FReal CullDistance, const FCollisionContext& Context)
		{
			const FRigidTransform3 Transform0 = GetTransform(Constraint.Particle[0]);
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);

			UpdateManifold(Constraint, Transform0, Transform1, CullDistance, Context);
		}

		void Update(FRigidBodyPointContactConstraint& Constraint, const FReal CullDistance)
		{
			const FRigidTransform3 Transform0 = GetTransform(Constraint.Particle[0]);
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);

			Constraint.ResetPhi(CullDistance);
			UpdateConstraintFromGeometry<ECollisionUpdateType::Deepest>(Constraint, Transform0, Transform1, CullDistance);
		}

		void Update(FRigidBodySweptPointContactConstraint& Constraint, const FReal CullDistance)
		{
			// Update as a point constraint (base class).
			Update(*Constraint.As<FRigidBodyPointContactConstraint>(), CullDistance);
		}

		void Update(FRigidBodyMultiPointContactConstraint& Constraint, const FReal CullDistance)
		{
			const FRigidTransform3 Transform0 = GetTransform(Constraint.Particle[0]);
			const FRigidTransform3 Transform1 = GetTransform(Constraint.Particle[1]);

			Constraint.ResetPhi(CullDistance);
			UpdateConstraintFromManifold(Constraint, Transform0, Transform1, CullDistance);
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
			, bool bIsRigidDynamic0
			, bool bIsRigidDynamic1
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
			if (!bIsRigidDynamic0 && bIsRigidDynamic1)
			{
				FMatrix33 WorldSpaceI2 = Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->I());
				FVec3 ImpulseDelta = PBDRigid1->M() * FVec3::CrossProduct(VectorToPoint2, Delta);
				Impulse += ImpulseDelta;
				AngularImpulse += WorldSpaceI2 * Delta - FVec3::CrossProduct(VectorToPoint2, ImpulseDelta);
			}
			else if (bIsRigidDynamic0 && !bIsRigidDynamic1)
			{
				FMatrix33 WorldSpaceI1 = Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->I());
				FVec3 ImpulseDelta = PBDRigid0->M() * FVec3::CrossProduct(VectorToPoint1, Delta);
				Impulse += ImpulseDelta;
				AngularImpulse += WorldSpaceI1 * Delta - FVec3::CrossProduct(VectorToPoint1, ImpulseDelta);
			}
			else if (bIsRigidDynamic0 && bIsRigidDynamic1)
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
		
		FVec3 ApplyContactAccImpulse(FCollisionContact& Contact,
			TGenericParticleHandle<FReal, 3> Particle0,
			TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			FVec3& AccumulatedImpulse)
		{

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();

			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			const FVec3 ZeroVector = FVec3(0);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			const FVec3 VectorToPoint1 = Contact.Location - P0;
			const FVec3 VectorToPoint2 = Contact.Location - P1;
			const FVec3 Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, VectorToPoint1);
			const FVec3 Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, VectorToPoint2);
			const FVec3 RelativeVelocity = Body1Velocity - Body2Velocity;  // Do not early out on negative normal velocity since there can still be an accumulated impulse
			*IterationParameters.NeedsAnotherIteration = true;

			const FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) : FMatrix33(0);
			const FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) : FMatrix33(0);
			const FMatrix33 Factor =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : FMatrix33(0));
			FVec3 AngularImpulse(0);

			// Resting contact if very close to the surface
			const bool bApplyRestitution = (RelativeVelocity.Size() > (2 * 980 * IterationParameters.Dt));
			const FReal Restitution = (bApplyRestitution) ? Contact.Restitution : (FReal)0;
			const FReal Friction = Contact.Friction;
			const FReal AngularFriction = Contact.AngularFriction;

			const FVec3 VelocityTarget = (-Restitution*FVec3::DotProduct(RelativeVelocity, Contact.Normal)) * Contact.Normal;
			const FVec3 VelocityChange = VelocityTarget - RelativeVelocity;
			const FMatrix33 FactorInverse = Factor.Inverse();
			FVec3 Impulse = FactorInverse * VelocityChange;  // Delta Impulse = (J.M^(-1).J^(T))^(-1).(ContactVelocityError)

			// clip the impulse so that the accumulated impulse is not in the wrong direction and in the friction cone
			// Clipping the accumulated impulse instead of the delta impulse (for the current iteration) is very important for jitter
			const FVec3 NewUnclippedAccumulatedImpulse = AccumulatedImpulse + Impulse;
			FVec3 ClippedAccumulatedImpulse(0.0f);  // To be calculated
			{
				//Project to normal
				const float NewAccImpNormalSize = FVec3::DotProduct(NewUnclippedAccumulatedImpulse, Contact.Normal);
				if (NewAccImpNormalSize > 0) // Clipping impulses to be positive
				{
					if (Friction <= 0)
					{
						ClippedAccumulatedImpulse = NewAccImpNormalSize * Contact.Normal;
					}
					else
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
									bIsRigidDynamic0,
									bIsRigidDynamic1,
									Q0,
									Q1,
									WorldSpaceInvI1,
									WorldSpaceInvI2);
							}
							ClippedAccumulatedImpulse = Impulse + AccumulatedImpulse;
						}
						else
						{
							ClippedAccumulatedImpulse = (MaximumImpulseTangential / ImpulseTangentialSize) * ImpulseTangential + NewAccImpNormalSize * Contact.Normal;
						}
					}
				}
				else
				{
					// Clipped Impulse is 0
					ClippedAccumulatedImpulse = FVec3(0); // Important case: the delta impulse should remove the accumulated impulse
				}
			}

			FVec3 OutDeltaImpulse = ClippedAccumulatedImpulse - AccumulatedImpulse;
			if (Chaos_Collision_EnergyClampEnabled != 0)
			{
				// Clamp the delta impulse to make sure we don't gain kinetic energy (ignore potential energy)
				// Todo: Investigate Energy clamping to work with accumulated impulses
				// else this might introduce a small amount of jitter, since we are clamping delta impulses instead of accumulated ones
				OutDeltaImpulse = GetEnergyClampedImpulse(Particle0->CastToRigidParticle(), Particle1->CastToRigidParticle(), OutDeltaImpulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
			}

			AccumulatedImpulse += OutDeltaImpulse; // Now update the accumulated Impulse
			if (bIsRigidDynamic0)
			{
				// Velocity update for next step
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint1, OutDeltaImpulse) + AngularImpulse;
				const FVec3 DV = PBDRigid0->InvM() * OutDeltaImpulse;
				const FVec3 DW = WorldSpaceInvI1 * NetAngularImpulse;
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
				const FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint2, -OutDeltaImpulse) - AngularImpulse;
				const FVec3 DV = -PBDRigid1->InvM() * OutDeltaImpulse;
				const FVec3 DW = WorldSpaceInvI2 * NetAngularImpulse;
				PBDRigid1->V() += DV;
				PBDRigid1->W() += DW;
				// Position update as part of pbd
				P1 += (DV * IterationParameters.Dt);
				Q1 += FRotation3::FromElements(DW, 0.f) * Q1 * IterationParameters.Dt * FReal(0.5);
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
			}
			
			return AccumulatedImpulse;
		}

		// Apply contacts, impulse clipping is done on delta impulses as apposed to Accumulated impulses
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

			const FVec3 ZeroVector = FVec3(0);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

			FVec3 VectorToPoint1 = Contact.Location - P0;
			FVec3 VectorToPoint2 = Contact.Location - P1;
			FVec3 Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, VectorToPoint1);
			FVec3 Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, VectorToPoint2);
			FVec3 RelativeVelocity = Body1Velocity - Body2Velocity;
			FReal RelativeNormalVelocity = FVec3::DotProduct(RelativeVelocity, Contact.Normal);

			if (RelativeNormalVelocity < 0) // ignore separating constraints
			{
				*IterationParameters.NeedsAnotherIteration = true;

				FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
				FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
				FMatrix33 Factor =
					(bIsRigidDynamic0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : FMatrix33(0)) +
					(bIsRigidDynamic1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : FMatrix33(0));
				FVec3 Impulse;
				FVec3 AngularImpulse(0);

				// Resting contact if very close to the surface
				bool bApplyRestitution = (RelativeVelocity.Size() > (2 * 980 * IterationParameters.Dt));
				FReal Restitution = (bApplyRestitution) ? Contact.Restitution : (FReal)0;
				FReal Friction = Contact.Friction;
				FReal AngularFriction = Contact.AngularFriction;

				if (Friction > 0)
				{
					FVec3 VelocityChange = -(Restitution * RelativeNormalVelocity * Contact.Normal + RelativeVelocity);
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
								bIsRigidDynamic0,
								bIsRigidDynamic1,
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

						const FReal ImpulseMag = -(1 + Restitution) * RelativeNormalVelocity / ImpulseDenominator;
						Impulse = ImpulseMag * (Contact.Normal - Friction * Tangent);
					}
				}
				else
				{
					FReal ImpulseDenominator = FVec3::DotProduct(Contact.Normal, Factor * Contact.Normal);
					FVec3 ImpulseNumerator = -(1 + Restitution) * FVec3::DotProduct(RelativeVelocity, Contact.Normal)* Contact.Normal;
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
				AccumulatedImpulse += Impulse;

				if (bIsRigidDynamic0)
				{
					// Velocity update for next step
					FVec3 NetAngularImpulse = FVec3::CrossProduct(VectorToPoint1, Impulse) + AngularImpulse;
					FVec3 DV = PBDRigid0->InvM() * Impulse;
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
					FVec3 DV = -PBDRigid1->InvM() * Impulse;
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

#if INTEL_ISPC
			if (bChaos_Collision_ISPC_Enabled)
			{
				FVec3 PActor0 = Particle0->P();
				FRotation3 QActor0 = Particle0->Q();
				FVec3 PActor1 = Particle1->P();
				FRotation3 QActor1 = Particle1->Q();
				const FReal InvM0 = Particle0->InvM();
				const FVec3 InvI0 = Particle0->InvI().GetDiagonal();
				const FVec3 PCoM0 = Particle0->CenterOfMass();
				const FRotation3 QCoM0 = Particle0->RotationOfMass();
				const FReal InvM1 = Particle1->InvM();
				const FVec3 InvI1 = Particle1->InvI().GetDiagonal();
				const FVec3 PCoM1 = Particle1->CenterOfMass();
				const FRotation3 QCoM1 = Particle1->RotationOfMass();
				ispc::ApplyContact2(
					(ispc::FCollisionContact*)&Contact,
					(ispc::FVector&)PActor0,
					(ispc::FVector4&)QActor0,
					(ispc::FVector&)PActor1,
					(ispc::FVector4&)QActor1,
					InvM0,
					(const ispc::FVector&)InvI0,
					(const ispc::FVector&)PCoM0,
					(const ispc::FVector4&)QCoM0,
					InvM1,
					(const ispc::FVector&)InvI1,
					(const ispc::FVector&)PCoM1,
					(const ispc::FVector4&)QCoM1);

				if (bIsRigidDynamic0)
				{
					PBDRigid0->SetP(PActor0);
					PBDRigid0->SetQ(QActor0);
				}
				if (bIsRigidDynamic1)
				{
					PBDRigid1->SetP(PActor1);
					PBDRigid1->SetQ(QActor1);
				}

				*IterationParameters.NeedsAnotherIteration = true;
				return AccumulatedImpulse;
			}
#endif

			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
			FVec3 VectorToPoint0 = Contact.Location - P0;
			FVec3 VectorToPoint1 = Contact.Location - P1;

			if (Contact.Phi < 0)
			{
				*IterationParameters.NeedsAnotherIteration = true;
			
				FReal InvM0 = bIsRigidDynamic0 ? PBDRigid0->InvM() : 0.0f;
				FReal InvM1 = bIsRigidDynamic1 ? PBDRigid1->InvM() : 0.0f;
				FMatrix33 InvI0 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
				FMatrix33 InvI1 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
				FMatrix33 ContactInvI =
					(bIsRigidDynamic0 ? ComputeFactorMatrix3(VectorToPoint0, InvI0, InvM0) : FMatrix33(0)) +
					(bIsRigidDynamic1 ? ComputeFactorMatrix3(VectorToPoint1, InvI1, InvM1) : FMatrix33(0));

				// Calculate the normal correction
				// @todo(chaos): support restitution in this mode (requires a small-impact cutoff threshold)
				FReal Restitution = 0.0f;//Contact.Restitution;
				FVec3 NormalError = Contact.Phi * Contact.Normal;
				FReal NormalImpulseDenominator = FVec3::DotProduct(Contact.Normal, ContactInvI * Contact.Normal);
				FVec3 NormalImpulseNumerator = -(1.0f + Restitution) * NormalError;
				FVec3 NormalCorrection = NormalImpulseNumerator / NormalImpulseDenominator;

				// Calculate the lateral correction
				FVec3 LateralCorrection = FVec3(0);
				if (Contact.Friction > 0)
				{
					// Get contact velocity
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

					// Calculate lateral correction, clamped to the friction cone. Kinda.
					FReal CVNormalMag = FVec3::DotProduct(CV, Contact.Normal);
					if (CVNormalMag < 0.0f)
					{
						FVec3 CVLateral = CV - CVNormalMag * Contact.Normal;
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
				Collisions::Update(Constraint, ParticleParameters.CullDistance);

				// Permanently disable a constraint that is beyond the cull distance
				if (Constraint.GetPhi() >= ParticleParameters.CullDistance)
				{
					Constraint.SetDisabled(true);
					return;
				}

				// Do not early out here in the case of Accumulated impulse solve
				if (Constraint.GetPhi() >= ParticleParameters.ShapePadding && !Chaos_Collision_UseAccumulatedImpulseClipSolve)
				{
					return;
				}

				if (ParticleParameters.Collided)
				{
					Particle0->AuxilaryValue(*ParticleParameters.Collided) = true;
					Particle1->AuxilaryValue(*ParticleParameters.Collided) = true;
				}

				//
				// @todo(chaos) : Collision Constraints
				//   Consider applying all constraints in ::Apply at each iteration, right now it just takes the deepest.
				//   For example, and iterative constraint might have 4 penetrating points that need to be resolved. 
				//

				// What Apply algorithm should we use? Controlled by the solver, with forcable cvar override for now...
				bool bUseVelocityMode = (IterationParameters.ApplyType == ECollisionApplyType::Velocity);
				if (Chaos_Collision_ForceApplyType != 0)
				{
					bUseVelocityMode = (Chaos_Collision_ForceApplyType == (int32)ECollisionApplyType::Velocity);
				}

				if (bUseVelocityMode)
				{
					if (Chaos_Collision_UseAccumulatedImpulseClipSolve)
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
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[1]);

			if (IterationParameters.Iteration > 0 || Constraint.TimeOfImpact == 1)
			{
				// If not on first iteration, or at TOI = 1 (normal constraint) we don't want to split timestep at TOI.
				ApplyImpl(Constraint, IterationParameters, ParticleParameters);
				return;
			}

			// Rebuild iteration params with partial dt, and non-zero iteration count to force update of constraint.
			// P may have changed due to other constraints, so at TOI our manifold needs updating.
			const FReal PartialDT = Constraint.TimeOfImpact * IterationParameters.Dt;
			const FReal RemainingDT = (1 - Constraint.TimeOfImpact) * IterationParameters.Dt;
			const int32 FakeIteration = FMath::Max(IterationParameters.Iteration, 1); // Force Apply to update constraint, as other constraints could've changed P
			const FContactIterationParameters IterationParametersPartialDT{ PartialDT, FakeIteration, IterationParameters.NumIterations, IterationParameters.NumPairIterations, IterationParameters.ApplyType, IterationParameters.NeedsAnotherIteration };
			const FContactIterationParameters IterationParametersRemainingDT{ RemainingDT, FakeIteration, IterationParameters.NumIterations, IterationParameters.NumPairIterations, IterationParameters.ApplyType, IterationParameters.NeedsAnotherIteration };

			// Rewind P to TOI and Apply
			Particle0->P() = FMath::Lerp(Particle0->X(), Particle0->P(), Constraint.TimeOfImpact);
			ApplyImpl(Constraint, IterationParametersPartialDT, ParticleParameters);

			// Advance P to end of frame from TOI, and Apply
			Particle0->P() = Particle0->P() + Particle0->V() * RemainingDT;
			ApplyImpl(Constraint, IterationParametersRemainingDT, ParticleParameters);
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


		FVec3 ApplyPushOutContact(
			FCollisionContact& Contact,
			TGenericParticleHandle<FReal, 3> Particle0, 
			TGenericParticleHandle<FReal, 3> Particle1,
			const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			FVec3 AccumulatedImpulse(0);

			TPBDRigidParticleHandle<FReal, 3>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<FReal, 3>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			const FVec3 ZeroVector = FVec3(0);
			FVec3 P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			FVec3 P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			FRotation3 Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			FRotation3 Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
			const bool IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Particle0->GeometryParticleHandle());
			const bool IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Particle1->GeometryParticleHandle());

			if (Contact.Phi >= ParticleParameters.ShapePadding)
			{
				return AccumulatedImpulse;
			}

			if ((!bIsRigidDynamic0 || IsTemporarilyStatic0) && (!bIsRigidDynamic1 || IsTemporarilyStatic1))
			{
				return AccumulatedImpulse;
			}

			FMatrix33 WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) * Contact.InvInertiaScale0 : FMatrix33(0);
			FMatrix33 WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) * Contact.InvInertiaScale1 : FMatrix33(0);
			FVec3 VectorToPoint1 = Contact.Location - P0;
			FVec3 VectorToPoint2 = Contact.Location - P1;
			FMatrix33 Factor =
				(bIsRigidDynamic0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : FMatrix33(0)) +
				(bIsRigidDynamic1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : FMatrix33(0));
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
				if (!IsTemporarilyStatic0 && bIsRigidDynamic0)
				{
					FVec3 AngularImpulse = FVec3::CrossProduct(VectorToPoint1, VelocityFixImpulse);
					PBDRigid0->V() += PBDRigid0->InvM() * VelocityFixImpulse;
					PBDRigid0->W() += WorldSpaceInvI1 * AngularImpulse;

				}

				if (!IsTemporarilyStatic1 && bIsRigidDynamic1)
				{
					FVec3 AngularImpulse = FVec3::CrossProduct(VectorToPoint2, -VelocityFixImpulse);
					PBDRigid1->V() -= PBDRigid1->InvM() * VelocityFixImpulse;
					PBDRigid1->W() += WorldSpaceInvI2 * AngularImpulse;
				}

			}


			FVec3 Impulse = FMatrix33(Factor.Inverse()) * ((-Contact.Phi + ParticleParameters.ShapePadding) * ScalingFactor * Contact.Normal);
			FVec3 AngularImpulse1 = FVec3::CrossProduct(VectorToPoint1, Impulse);
			FVec3 AngularImpulse2 = FVec3::CrossProduct(VectorToPoint2, -Impulse);
			if (!IsTemporarilyStatic0 && bIsRigidDynamic0)
			{
				P0 += PBDRigid0->InvM() * Impulse;
				Q0 = FRotation3::FromVector(WorldSpaceInvI1 * AngularImpulse1) * Q0;
				Q0.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
			}
			if (!IsTemporarilyStatic1 && bIsRigidDynamic1)
			{
				P1 -= PBDRigid1->InvM() * Impulse;
				Q1 = FRotation3::FromVector(WorldSpaceInvI2 * AngularImpulse2) * Q1;
				Q1.Normalize();
				FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
			}

			return AccumulatedImpulse;
		}


		template<typename T_CONSTRAINT>
		void ApplyPushOutImpl(T_CONSTRAINT& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			TGenericParticleHandle<FReal, 3> Particle0 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[0]);
			TGenericParticleHandle<FReal, 3> Particle1 = TGenericParticleHandle<FReal, 3>(Constraint.Particle[1]);

			for (int32 PairIt = 0; PairIt < IterationParameters.NumPairIterations; ++PairIt)
			{
				Update(Constraint, ParticleParameters.CullDistance);

				// Permanently disable a constraint that is beyond the cull distance
				if (Constraint.GetPhi() >= ParticleParameters.CullDistance)
				{
					Constraint.SetDisabled(true);
					return;
				}

				if (Constraint.GetPhi() >= ParticleParameters.ShapePadding)
				{
					return;
				}

				Constraint.AccumulatedImpulse += 
					ApplyPushOutContact(Constraint.Manifold, Particle0, Particle1, IsTemporarilyStatic, IterationParameters, ParticleParameters);
			}
		}

		void ApplyPushOut(FCollisionConstraintBase& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic, 
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			if (Constraint.GetType() == FCollisionConstraintBase::FType::SinglePoint)
			{
				ApplyPushOutImpl<FRigidBodyPointContactConstraint>(*Constraint.As<FRigidBodyPointContactConstraint>(), IsTemporarilyStatic, IterationParameters, ParticleParameters);
			}
			else if (Constraint.GetType() == FCollisionConstraintBase::FType::SinglePointSwept)
			{
				ApplyPushOutImpl(*Constraint.As<FRigidBodySweptPointContactConstraint>(), IsTemporarilyStatic, IterationParameters, ParticleParameters);
			}
			else if (Constraint.GetType() == FCollisionConstraintBase::FType::MultiPoint)
			{
				ApplyPushOutImpl<FRigidBodyMultiPointContactConstraint>(*Constraint.As<FRigidBodyMultiPointContactConstraint>(), IsTemporarilyStatic, IterationParameters, ParticleParameters);
			}
		}

		void ApplyPushOutSinglePoint(FRigidBodyPointContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			ApplyPushOutImpl<FRigidBodyPointContactConstraint>(Constraint, IsTemporarilyStatic, IterationParameters, ParticleParameters);
		}

		void ApplyPushOutMultiPoint(FRigidBodyMultiPointContactConstraint& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters & IterationParameters, const FContactParticleParameters & ParticleParameters)
		{
			ApplyPushOutImpl<FRigidBodyMultiPointContactConstraint>(Constraint, IsTemporarilyStatic, IterationParameters, ParticleParameters);
		}

	} // Collisions

}// Chaos

