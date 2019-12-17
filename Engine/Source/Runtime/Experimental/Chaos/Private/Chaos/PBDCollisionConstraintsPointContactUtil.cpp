// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraintsPointContactUtil.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Defines.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/Utilities.h"

namespace Chaos
{
	namespace Collisions
	{
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void Update(const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint)
		{
			Constraint.ResetPhi(Thickness);
			const TRigidTransform<T, d> ParticleTM = GetTransform(Constraint.Particle[0]);
			const TRigidTransform<T, d> LevelsetTM = GetTransform(Constraint.Particle[1]);

			if (!Constraint.Particle[0]->Geometry())
			{
				if (Constraint.Particle[1]->Geometry())
				{
					if (!Constraint.Particle[1]->Geometry()->IsUnderlyingUnion())
					{
						UpdateLevelsetLevelsetConstraint<UpdateType>(Thickness, Constraint);
					}
					else
					{
						UpdateUnionLevelsetConstraint<UpdateType>(Thickness, Constraint);
					}
				}
			}
			else
			{
				UpdateConstraintImp<UpdateType>(*Constraint.Particle[0]->Geometry(), ParticleTM, *Constraint.Particle[1]->Geometry(), LevelsetTM, Thickness, Constraint);
			}
		}

		template<typename T, int d>
		void Apply(TRigidBodyPointContactConstraint<T, d>& Constraint, T Thickness, TPointContactIterationParameters<T> & IterationParameters, TPointContactParticleParameters<T> & ParticleParameters)
		{
			TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(Constraint.Particle[0]);
			TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(Constraint.Particle[1]);
			TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->CastToRigidParticle();
			bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			for (int32 PairIt = 0; PairIt < IterationParameters.NumPairIterations; ++PairIt)
			{
				Collisions::Update<ECollisionUpdateType::Deepest>(Thickness, Constraint);
				if (Constraint.GetPhi() >= Thickness)
				{
					return;
				}

				// @todo(ccaulfield): CHAOS_PARTICLEHANDLE_TODO what's the best way to manage external per-particle data?
				if (ParticleParameters.Collided)
				{
					Particle0->AuxilaryValue(*ParticleParameters.Collided) = true;
					Particle1->AuxilaryValue(*ParticleParameters.Collided) = true;
				}

				TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial0, PhysicsMaterial1;
				if (ParticleParameters.PhysicsMaterials)
				{
					PhysicsMaterial0 = Particle0->AuxilaryValue(*ParticleParameters.PhysicsMaterials);
					PhysicsMaterial1 = Particle1->AuxilaryValue(*ParticleParameters.PhysicsMaterials);
				}

				const TVector<T, d> ZeroVector = TVector<T, d>(0);
				TVector<T, d> P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
				TVector<T, d> P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
				TRotation<T, d> Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
				TRotation<T, d> Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);

				typename TRigidBodyPointContactConstraint<T, d>::FManifold & Contact = Constraint.Manifold;

				TVector<T, d> VectorToPoint1 = Contact.Location - P0;
				TVector<T, d> VectorToPoint2 = Contact.Location - P1;
				TVector<T, d> Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, VectorToPoint1);
				TVector<T, d> Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, VectorToPoint2);
				TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
				T RelativeNormalVelocity = TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal);

				if (RelativeNormalVelocity < 0) // ignore separating constraints
				{
					PMatrix<T, d, d> WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) : PMatrix<T, d, d>(0);
					PMatrix<T, d, d> WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) : PMatrix<T, d, d>(0);
					PMatrix<T, d, d> Factor =
						(bIsRigidDynamic0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : PMatrix<T, d, d>(0)) +
						(bIsRigidDynamic1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : PMatrix<T, d, d>(0));
					TVector<T, d> Impulse;
					TVector<T, d> AngularImpulse(0);

					// Resting contact if very close to the surface
					T Restitution = (T)0;
					T Friction = (T)0;
					T AngularFriction = (T)0;
					bool bApplyRestitution = (RelativeVelocity.Size() > (2 * 980 * IterationParameters.Dt));
					if (PhysicsMaterial0 && PhysicsMaterial1)
					{
						if (bApplyRestitution)
						{
							Restitution = FMath::Min(PhysicsMaterial0->Restitution, PhysicsMaterial1->Restitution);
						}
						Friction = FMath::Max(PhysicsMaterial0->Friction, PhysicsMaterial1->Friction);
					}
					else if (PhysicsMaterial0)
					{
						if (bApplyRestitution)
						{
							Restitution = PhysicsMaterial0->Restitution;
						}
						Friction = PhysicsMaterial0->Friction;
					}
					else if (PhysicsMaterial1)
					{
						if (bApplyRestitution)
						{
							Restitution = PhysicsMaterial1->Restitution;
						}
						Friction = PhysicsMaterial1->Friction;
					}

					if (ParticleParameters.FrictionOverride >= 0)
					{
						Friction = ParticleParameters.FrictionOverride;
					}
					if (ParticleParameters.AngularFrictionOverride >= 0)
					{
						AngularFriction = ParticleParameters.AngularFrictionOverride;
					}

					if (Friction)
					{
						if (RelativeNormalVelocity > 0)
						{
							RelativeNormalVelocity = 0;
						}
						TVector<T, d> VelocityChange = -(Restitution * RelativeNormalVelocity * Contact.Normal + RelativeVelocity);
						T NormalVelocityChange = TVector<T, d>::DotProduct(VelocityChange, Contact.Normal);
						PMatrix<T, d, d> FactorInverse = Factor.Inverse();
						TVector<T, d> MinimalImpulse = FactorInverse * VelocityChange;
						const T MinimalImpulseDotNormal = TVector<T, d>::DotProduct(MinimalImpulse, Contact.Normal);
						const T TangentialSize = (MinimalImpulse - MinimalImpulseDotNormal * Contact.Normal).Size();
						if (TangentialSize <= Friction * MinimalImpulseDotNormal)
						{
							//within friction cone so just solve for static friction stopping the object
							Impulse = MinimalImpulse;
							if (AngularFriction)
							{
								TVector<T, d> RelativeAngularVelocity = Particle0->W() - Particle1->W();
								T AngularNormal = TVector<T, d>::DotProduct(RelativeAngularVelocity, Contact.Normal);
								TVector<T, d> AngularTangent = RelativeAngularVelocity - AngularNormal * Contact.Normal;
								TVector<T, d> FinalAngularVelocity = FMath::Sign(AngularNormal) * FMath::Max((T)0, FMath::Abs(AngularNormal) - AngularFriction * NormalVelocityChange) * Contact.Normal + FMath::Max((T)0, AngularTangent.Size() - AngularFriction * NormalVelocityChange) * AngularTangent.GetSafeNormal();
								TVector<T, d> Delta = FinalAngularVelocity - RelativeAngularVelocity;
								if (!bIsRigidDynamic0 && bIsRigidDynamic1)
								{
									PMatrix<T, d, d> WorldSpaceI2 = (Q1 * FMatrix::Identity) * PBDRigid1->I() * (Q1 * FMatrix::Identity).GetTransposed();
									TVector<T, d> ImpulseDelta = PBDRigid1->M() * TVector<T, d>::CrossProduct(VectorToPoint2, Delta);
									Impulse += ImpulseDelta;
									AngularImpulse += WorldSpaceI2 * Delta - TVector<T, d>::CrossProduct(VectorToPoint2, ImpulseDelta);
								}
								else if (bIsRigidDynamic0 && !bIsRigidDynamic1)
								{
									PMatrix<T, d, d> WorldSpaceI1 = (Q0 * FMatrix::Identity) * PBDRigid0->I() * (Q0 * FMatrix::Identity).GetTransposed();
									TVector<T, d> ImpulseDelta = PBDRigid0->M() * TVector<T, d>::CrossProduct(VectorToPoint1, Delta);
									Impulse += ImpulseDelta;
									AngularImpulse += WorldSpaceI1 * Delta - TVector<T, d>::CrossProduct(VectorToPoint1, ImpulseDelta);
								}
								else if (bIsRigidDynamic0 && bIsRigidDynamic1)
								{
									PMatrix<T, d, d> Cross1(0, VectorToPoint1.Z, -VectorToPoint1.Y, -VectorToPoint1.Z, 0, VectorToPoint1.X, VectorToPoint1.Y, -VectorToPoint1.X, 0);
									PMatrix<T, d, d> Cross2(0, VectorToPoint2.Z, -VectorToPoint2.Y, -VectorToPoint2.Z, 0, VectorToPoint2.X, VectorToPoint2.Y, -VectorToPoint2.X, 0);
									PMatrix<T, d, d> CrossI1 = Cross1 * WorldSpaceInvI1;
									PMatrix<T, d, d> CrossI2 = Cross2 * WorldSpaceInvI2;
									PMatrix<T, d, d> Diag1 = CrossI1 * Cross1.GetTransposed() + CrossI2 * Cross2.GetTransposed();
									Diag1.M[0][0] += PBDRigid0->InvM() + PBDRigid1->InvM();
									Diag1.M[1][1] += PBDRigid0->InvM() + PBDRigid1->InvM();
									Diag1.M[2][2] += PBDRigid0->InvM() + PBDRigid1->InvM();
									PMatrix<T, d, d> OffDiag1 = (CrossI1 + CrossI2) * -1;
									PMatrix<T, d, d> Diag2 = (WorldSpaceInvI1 + WorldSpaceInvI2).Inverse();
									PMatrix<T, d, d> OffDiag1Diag2 = OffDiag1 * Diag2;
									TVector<T, d> ImpulseDelta = PMatrix<T, d, d>((Diag1 - OffDiag1Diag2 * OffDiag1.GetTransposed()).Inverse())* ((OffDiag1Diag2 * -1) * Delta);
									Impulse += ImpulseDelta;
									AngularImpulse += Diag2 * (Delta - PMatrix<T, d, d>(OffDiag1.GetTransposed()) * ImpulseDelta);
								}
							}
						}
						else
						{
							//outside friction cone, solve for normal relative velocity and keep tangent at cone edge
							TVector<T, d> Tangent = (RelativeVelocity - TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal) * Contact.Normal).GetSafeNormal();
							TVector<T, d> DirectionalFactor = Factor * (Contact.Normal - Friction * Tangent);
							T ImpulseDenominator = TVector<T, d>::DotProduct(Contact.Normal, DirectionalFactor);
							if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nDirectionalFactor:%s, ImpulseDenominator:%f"),
								*Constraint.ToString(),
								*Particle0->ToString(),
								*Particle1->ToString(),
								*DirectionalFactor.ToString(), ImpulseDenominator))
							{
								ImpulseDenominator = (T)1;
							}

							const T ImpulseMag = -(1 + Restitution) * RelativeNormalVelocity / ImpulseDenominator;
							Impulse = ImpulseMag * (Contact.Normal - Friction * Tangent);
						}
					}
					else
					{
						T ImpulseDenominator = TVector<T, d>::DotProduct(Contact.Normal, Factor * Contact.Normal);
						TVector<T, d> ImpulseNumerator = -(1 + Restitution) * TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal)* Contact.Normal;
						if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Constraint.Normal:%s, ImpulseDenominator:%f"),
							*Constraint.ToString(),
							*Particle0->ToString(),
							*Particle1->ToString(),
							*(Factor * Contact.Normal).ToString(), ImpulseDenominator))
						{
							ImpulseDenominator = (T)1;
						}
						Impulse = ImpulseNumerator / ImpulseDenominator;
					}
					Impulse = GetEnergyClampedImpulse(Constraint, Impulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
					Constraint.AccumulatedImpulse += Impulse;
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
						Q0 += FRotation3::FromElements(DW, 0.f) * Q0 * IterationParameters.Dt * T(0.5);
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
						Q1 += FRotation3::FromElements(DW, 0.f) * Q1 * IterationParameters.Dt * T(0.5);
						Q1.Normalize();
						FParticleUtilities::SetCoMWorldTransform(PBDRigid1, P1, Q1);
					}
				}
			}
		}

		// @todo(ccaulfield): Kinematic-Dynamic optimized version
		template<typename T, int d>
		void ApplyPushOut(TRigidBodyPointContactConstraint<T, d>& Constraint, T Thickness, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic,
			TPointContactIterationParameters<T> & IterationParameters, TPointContactParticleParameters<T> & ParticleParameters)
		{
			TGenericParticleHandle<T, d> Particle0 = TGenericParticleHandle<T, d>(Constraint.Particle[0]);
			TGenericParticleHandle<T, d> Particle1 = TGenericParticleHandle<T, d>(Constraint.Particle[1]);
			TPBDRigidParticleHandle<T, d>* PBDRigid0 = Particle0->CastToRigidParticle();
			TPBDRigidParticleHandle<T, d>* PBDRigid1 = Particle1->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			const TVector<T, d> ZeroVector = TVector<T, d>(0);
			TVector<T, d> P0 = FParticleUtilities::GetCoMWorldPosition(Particle0);
			TVector<T, d> P1 = FParticleUtilities::GetCoMWorldPosition(Particle1);
			TRotation<T, d> Q0 = FParticleUtilities::GetCoMWorldRotation(Particle0);
			TRotation<T, d> Q1 = FParticleUtilities::GetCoMWorldRotation(Particle1);
			const bool IsTemporarilyStatic0 = IsTemporarilyStatic.Contains(Constraint.Particle[0]);
			const bool IsTemporarilyStatic1 = IsTemporarilyStatic.Contains(Constraint.Particle[1]);
	
			TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial0, PhysicsMaterial1;
			if (ParticleParameters.PhysicsMaterials)
			{
				PhysicsMaterial0 = Particle0->AuxilaryValue(*ParticleParameters.PhysicsMaterials);
				PhysicsMaterial1 = Particle1->AuxilaryValue(*ParticleParameters.PhysicsMaterials);
			}

			for (int32 PairIteration = 0; PairIteration < IterationParameters.NumPairIterations; ++PairIteration)
			{
				Update<ECollisionUpdateType::Deepest>(Thickness, Constraint);

				const typename TRigidBodyPointContactConstraint<T, d>::FManifold & Contact = Constraint.Manifold;

				if (Contact.Phi >= Thickness)
				{
					break;
				}

				if ((!bIsRigidDynamic0 || IsTemporarilyStatic0) && (!bIsRigidDynamic1 || IsTemporarilyStatic1))
				{	
					break;
				}

				*IterationParameters.NeedsAnotherIteration = true;
				PMatrix<T, d, d> WorldSpaceInvI1 = bIsRigidDynamic0 ? Utilities::ComputeWorldSpaceInertia(Q0, PBDRigid0->InvI()) : PMatrix<T, d, d>(0);
				PMatrix<T, d, d> WorldSpaceInvI2 = bIsRigidDynamic1 ? Utilities::ComputeWorldSpaceInertia(Q1, PBDRigid1->InvI()) : PMatrix<T, d, d>(0);
				TVector<T, d> VectorToPoint1 = Contact.Location - P0;
				TVector<T, d> VectorToPoint2 = Contact.Location - P1;
				PMatrix<T, d, d> Factor =
					(bIsRigidDynamic0 ? ComputeFactorMatrix3(VectorToPoint1, WorldSpaceInvI1, PBDRigid0->InvM()) : PMatrix<T, d, d>(0)) +
					(bIsRigidDynamic1 ? ComputeFactorMatrix3(VectorToPoint2, WorldSpaceInvI2, PBDRigid1->InvM()) : PMatrix<T, d, d>(0));
				T Numerator = FMath::Min((T)(IterationParameters.Iteration + 2), (T)IterationParameters.NumIterations);
				T ScalingFactor = Numerator / (T)IterationParameters.NumIterations;

				//if pushout is needed we better fix relative velocity along normal. Treat it as if 0 restitution
				TVector<T, d> Body1Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle0, VectorToPoint1);
				TVector<T, d> Body2Velocity = FParticleUtilities::GetVelocityAtCoMRelativePosition(Particle1, VectorToPoint2);
				TVector<T, d> RelativeVelocity = Body1Velocity - Body2Velocity;
				const T RelativeVelocityDotNormal = TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal);
				if (RelativeVelocityDotNormal < 0)
				{
					T ImpulseDenominator = TVector<T, d>::DotProduct(Contact.Normal, Factor * Contact.Normal);
					TVector<T, d> ImpulseNumerator = -TVector<T, d>::DotProduct(RelativeVelocity, Contact.Normal) * Contact.Normal * ScalingFactor;
					if (!ensureMsgf(FMath::Abs(ImpulseDenominator) > SMALL_NUMBER, TEXT("ApplyPushout Constraint:%s\n\nParticle:%s\n\nLevelset:%s\n\nFactor*Contact.Normal:%s, ImpulseDenominator:%f"),
						*Constraint.ToString(),
						*Particle0->ToString(),
						*Particle1->ToString(),
						*(Factor*Contact.Normal).ToString(), ImpulseDenominator))
					{
						ImpulseDenominator = (T)1;
					}

					TVector<T, d> VelocityFixImpulse = ImpulseNumerator / ImpulseDenominator;
					VelocityFixImpulse = GetEnergyClampedImpulse(Constraint, VelocityFixImpulse, VectorToPoint1, VectorToPoint2, Body1Velocity, Body2Velocity);
					Constraint.AccumulatedImpulse += VelocityFixImpulse;	//question: should we track this?
					if (!IsTemporarilyStatic0 && bIsRigidDynamic0)
					{
						TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint1, VelocityFixImpulse);
						PBDRigid0->V() += PBDRigid0->InvM() * VelocityFixImpulse;
						PBDRigid0->W() += WorldSpaceInvI1 * AngularImpulse;

					}

					if (!IsTemporarilyStatic1 && bIsRigidDynamic1)
					{	
						TVector<T, d> AngularImpulse = TVector<T, d>::CrossProduct(VectorToPoint2, -VelocityFixImpulse);
						PBDRigid1->V() -= PBDRigid1->InvM() * VelocityFixImpulse;
						PBDRigid1->W() += WorldSpaceInvI2 * AngularImpulse;
					}

				}


				TVector<T, d> Impulse = PMatrix<T, d, d>(Factor.Inverse()) * ((-Contact.Phi + Thickness) * ScalingFactor * Contact.Normal);
				TVector<T, d> AngularImpulse1 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
				TVector<T, d> AngularImpulse2 = TVector<T, d>::CrossProduct(VectorToPoint2, -Impulse);
				if (!IsTemporarilyStatic0 && bIsRigidDynamic0)
				{
					P0 += PBDRigid0->InvM() * Impulse;
					Q0 = TRotation<T, d>::FromVector(WorldSpaceInvI1 * AngularImpulse1) * Q0;
					Q0.Normalize();
					FParticleUtilities::SetCoMWorldTransform(Particle0, P0, Q0);
				}
				if (!IsTemporarilyStatic1 && bIsRigidDynamic1)
				{
					P1 -= PBDRigid1->InvM() * Impulse;
					Q1 = TRotation<T, d>::FromVector(WorldSpaceInvI2 * AngularImpulse2) * Q1;
					Q1.Normalize();
					FParticleUtilities::SetCoMWorldTransform(Particle1, P1, Q1);
				}
			}
		}

		template void Update<ECollisionUpdateType::Any, float, 3>(const float, TRigidBodyPointContactConstraint<float, 3>&);
		template void Update<ECollisionUpdateType::Deepest, float, 3>(const float, TRigidBodyPointContactConstraint<float, 3>&);
		template void Apply<float, 3>(TRigidBodyPointContactConstraint<float, 3>&, float, TPointContactIterationParameters<float> &, TPointContactParticleParameters<float> &);
		template void ApplyPushOut<float, 3>(TRigidBodyPointContactConstraint<float,3>&, float , const TSet<const TGeometryParticleHandle<float,3>*>&,
			TPointContactIterationParameters<float> & IterationParameters, TPointContactParticleParameters<float> & ParticleParameters);

	} // Collisions

}// Chaos

