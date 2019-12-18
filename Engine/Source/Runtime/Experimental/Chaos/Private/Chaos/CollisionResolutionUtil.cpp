// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolutionUtil.h"

#include "Chaos/ChaosPerfTest.h"
#include "Chaos/Defines.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/SpatialAccelerationCollection.h"
#include "Chaos/Levelset.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Stats/Stats.h" 

#if INTEL_ISPC
#include "PBDCollisionConstraints.ispc.generated.h"
#endif

namespace Chaos
{
	namespace Collisions
	{

		template<typename T, int d>
		TRigidTransform<T, d> GetTransform(const TGeometryParticleHandle<T, d>* Particle)
		{
			TGenericParticleHandle<T, d> Generic = const_cast<TGeometryParticleHandle<T, d>*>(Particle);
			return TRigidTransform<T, d>(Generic->P(), Generic->Q());
		}

		// @todo(ccaulfield): This is duplicated in JointConstraints - move to a utility file
		template<typename T>
		PMatrix<T, 3, 3> ComputeFactorMatrix3(const TVector<T, 3>& V, const PMatrix<T, 3, 3>& M, const T& Im)
		{
			// Rigid objects rotational contribution to the impulse.
			// Vx*M*VxT+Im
			ensure(Im > FLT_MIN);
				return PMatrix<T, 3, 3>(
					-V[2] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]) + V[1] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]) + Im,
					V[2] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) - V[0] * (-V[2] * M.M[2][1] + V[1] * M.M[2][2]),
					-V[1] * (-V[2] * M.M[1][0] + V[1] * M.M[2][0]) + V[0] * (-V[2] * M.M[1][1] + V[1] * M.M[2][1]),
					V[2] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) - V[0] * (V[2] * M.M[2][0] - V[0] * M.M[2][2]) + Im,
					-V[1] * (V[2] * M.M[0][0] - V[0] * M.M[2][0]) + V[0] * (V[2] * M.M[1][0] - V[0] * M.M[2][1]),
					-V[1] * (-V[1] * M.M[0][0] + V[0] * M.M[1][0]) + V[0] * (-V[1] * M.M[1][0] + V[0] * M.M[1][1]) + Im);
		}

		template<typename T, int d>
		TVector<T, d> GetEnergyClampedImpulse(const TCollisionConstraintBase<T, d>& Constraint, const TVector<T, d>& Impulse, const TVector<T, d>& VectorToPoint1, const TVector<T, d>& VectorToPoint2, const TVector<T, d>& Velocity1, const TVector<T, d>& Velocity2)
		{
			TPBDRigidParticleHandle<T, d>* PBDRigid0 = Constraint.Particle[0]->CastToRigidParticle();
			TPBDRigidParticleHandle<T, d>* PBDRigid1 = Constraint.Particle[1]->CastToRigidParticle();
			const bool bIsRigidDynamic0 = PBDRigid0 && PBDRigid0->ObjectState() == EObjectStateType::Dynamic;
			const bool bIsRigidDynamic1 = PBDRigid1 && PBDRigid1->ObjectState() == EObjectStateType::Dynamic;

			TVector<T, d> Jr0, Jr1, IInvJr0, IInvJr1;
			T ImpulseRatioNumerator0 = 0, ImpulseRatioNumerator1 = 0, ImpulseRatioDenom0 = 0, ImpulseRatioDenom1 = 0;
			T ImpulseSize = Impulse.SizeSquared();
			TVector<T, d> KinematicVelocity = !bIsRigidDynamic0 ? Velocity1 : !bIsRigidDynamic1 ? Velocity2 : TVector<T, d>(0);
			if (bIsRigidDynamic0)
			{
				Jr0 = TVector<T, d>::CrossProduct(VectorToPoint1, Impulse);
				IInvJr0 = PBDRigid0->Q().RotateVector(PBDRigid0->InvI() * PBDRigid0->Q().UnrotateVector(Jr0));
				ImpulseRatioNumerator0 = TVector<T, d>::DotProduct(Impulse, PBDRigid0->V() - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr0, PBDRigid0->W());
				ImpulseRatioDenom0 = ImpulseSize / PBDRigid0->M() + TVector<T, d>::DotProduct(Jr0, IInvJr0);
			}
			if (bIsRigidDynamic1)
			{
				Jr1 = TVector<T, d>::CrossProduct(VectorToPoint2, Impulse);
				IInvJr1 = PBDRigid1->Q().RotateVector(PBDRigid1->InvI() * PBDRigid1->Q().UnrotateVector(Jr1));
				ImpulseRatioNumerator1 = TVector<T, d>::DotProduct(Impulse, PBDRigid1->V() - KinematicVelocity) + TVector<T, d>::DotProduct(IInvJr1, PBDRigid1->W());
				ImpulseRatioDenom1 = ImpulseSize / PBDRigid1->M() + TVector<T, d>::DotProduct(Jr1, IInvJr1);
			}
			T Numerator = -2 * (ImpulseRatioNumerator0 - ImpulseRatioNumerator1);
			if (Numerator < 0)
			{
				return TVector<T, d>(0);
			}
			check(Numerator >= 0);
			T Denominator = ImpulseRatioDenom0 + ImpulseRatioDenom1;
			return Numerator < Denominator ? (Impulse * Numerator / Denominator) : Impulse;
		}

		template <typename T, int d>
		bool SampleObjectHelper(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint)
		{
			TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
			TVector<T, d> LocalNormal;
			T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);

			TPointContactManifold<T, d> & Contact = Constraint.Manifold;
			if (LocalPhi < Contact.Phi)
			{
				Contact.Phi = LocalPhi;
				Contact.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
				Contact.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
				return true;
			}
			return false;
		}

		template <typename T, int d>
		bool SampleObjectNoNormal(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint)
		{
			TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
			TVector<T, d> LocalNormal;
			T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);

			TPointContactManifold<T, d> & Contact = Constraint.Manifold;
			if (LocalPhi < Contact.Phi)
			{
				Contact.Phi = LocalPhi;
				return true;
			}
			return false;
		}

		template <typename T, int d>
		bool SampleObjectNormalAverageHelper(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, T& TotalThickness, TRigidBodyPointContactConstraint<float, 3>& Constraint)
		{
			TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
			TVector<T, d> LocalNormal;
			T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
			T LocalThickness = LocalPhi - Thickness;

			TPointContactManifold<T, d> & Contact = Constraint.Manifold;
			if (LocalThickness < -KINDA_SMALL_NUMBER)
			{
				Contact.Location += LocalPoint * LocalThickness;
				TotalThickness += LocalThickness;
				return true;
			}
			return false;
		}

		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetPartial"), STAT_UpdateLevelsetPartial, STATGROUP_ChaosWide);
		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetFindParticles"), STAT_UpdateLevelsetFindParticles, STATGROUP_ChaosWide);
		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetBVHTraversal"), STAT_UpdateLevelsetBVHTraversal, STATGROUP_ChaosWide);
		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetSignedDistance"), STAT_UpdateLevelsetSignedDistance, STATGROUP_ChaosWide);
		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::UpdateLevelsetAll"), STAT_UpdateLevelsetAll, STATGROUP_ChaosWide);
		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::SampleObject"), STAT_SampleObject, STATGROUP_ChaosWide);

		int32 NormalAveraging = 1;
		FAutoConsoleVariableRef CVarNormalAveraging(TEXT("p.NormalAveraging2"), NormalAveraging, TEXT(""));

		int32 SampleMinParticlesForAcceleration = 2048;
		FAutoConsoleVariableRef CVarSampleMinParticlesForAcceleration(TEXT("p.SampleMinParticlesForAcceleration"), SampleMinParticlesForAcceleration, TEXT("The minimum number of particles needed before using an acceleration structure when sampling"));


#if INTEL_ISPC
		template<ECollisionUpdateType UpdateType>
		void SampleObject(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_SampleObject);
			TRigidBodyPointContactConstraint<float, 3> AvgConstraint = Constraint;

			TPointContactManifold<float, 3> & Contact = Constraint.Manifold;
			TPointContactManifold<float, 3> & AvgContact = AvgConstraint.Manifold;

			AvgConstraint.Particle[0] = Constraint.Particle[0];
			AvgConstraint.Particle[1] = Constraint.Particle[1];
			AvgContact.Location = TVector<float, 3>::ZeroVector;
			AvgContact.Normal = TVector<float, 3>::ZeroVector;
			AvgContact.Phi = Thickness;
			float TotalThickness = float(0);

			int32 DeepestParticle = -1;

			const TRigidTransform<float, 3>& SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
			int32 NumParticles = SampleParticles.Size();

			if (NumParticles > SampleMinParticlesForAcceleration && Object.HasBoundingBox())
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetPartial);
				TBox<float, 3> ImplicitBox = Object.BoundingBox().TransformedBox(ObjectTransform.GetRelativeTransform(SampleParticlesTransform));
				ImplicitBox.Thicken(Thickness);
				TArray<int32> PotentialParticles;
				{
					SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetFindParticles);
					PotentialParticles = SampleParticles.FindAllIntersections(ImplicitBox);
				}
				{
					SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetSignedDistance);

					if (Object.GetType(true) == ImplicitObjectType::LevelSet && PotentialParticles.Num() > 0)
					{
						//QUICK_SCOPE_CYCLE_COUNTER(STAT_LevelSet);
						const TLevelSet<float, 3>* LevelSet = Object.GetObject<TLevelSet<float, 3>>();
						const TUniformGrid<float, 3>& Grid = LevelSet->GetGrid();

						if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
						{
							ispc::SampleLevelSetNormalAverage(
								(ispc::FVector&)Grid.MinCorner(),
								(ispc::FVector&)Grid.MaxCorner(),
								(ispc::FVector&)Grid.Dx(),
								(ispc::FIntVector&)Grid.Counts(),
								(ispc::TArrayND*)&LevelSet->GetPhiArray(),
								(ispc::FTransform&)SampleToObjectTM,
								(ispc::FVector*)&SampleParticles.XArray()[0],
								&PotentialParticles[0],
								Thickness,
								TotalThickness,
								(ispc::FVector&)AvgContact.Location,
								PotentialParticles.Num());
						}
						else
						{
							ispc::SampleLevelSetNoNormal(
								(ispc::FVector&)Grid.MinCorner(),
								(ispc::FVector&)Grid.MaxCorner(),
								(ispc::FVector&)Grid.Dx(),
								(ispc::FIntVector&)Grid.Counts(),
								(ispc::TArrayND*)&LevelSet->GetPhiArray(),
								(ispc::FTransform&)SampleToObjectTM,
								(ispc::FVector*)&SampleParticles.XArray()[0],
								&PotentialParticles[0],
								DeepestParticle,
								AvgContact.Phi,
								PotentialParticles.Num());

							if (UpdateType == ECollisionUpdateType::Any)
							{
								Contact.Phi = AvgContact.Phi;
								return;
							}
						}
					}
					else if (Object.GetType(true) == ImplicitObjectType::Box && PotentialParticles.Num() > 0)
					{
						//QUICK_SCOPE_CYCLE_COUNTER(STAT_Box);
						const TBox<float, 3>* Box = Object.GetObject<Chaos::TBox<float, 3>>();

						if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
						{
							ispc::SampleBoxNormalAverage(
								(ispc::FVector&)Box->Min(),
								(ispc::FVector&)Box->Max(),
								(ispc::FTransform&)SampleToObjectTM,
								(ispc::FVector*)&SampleParticles.XArray()[0],
								&PotentialParticles[0],
								Thickness,
								TotalThickness,
								(ispc::FVector&)AvgContact.Location,
								PotentialParticles.Num());
						}
						else
						{
							ispc::SampleBoxNoNormal(
								(ispc::FVector&)Box->Min(),
								(ispc::FVector&)Box->Max(),
								(ispc::FTransform&)SampleToObjectTM,
								(ispc::FVector*)&SampleParticles.XArray()[0],
								&PotentialParticles[0],
								DeepestParticle,
								AvgContact.Phi,
								PotentialParticles.Num());

							if (UpdateType == ECollisionUpdateType::Any)
							{
								Contact.Phi = AvgContact.Phi;
								return;
							}
						}
					}
					else
					{
						//QUICK_SCOPE_CYCLE_COUNTER(STAT_Other);
						for (int32 i : PotentialParticles)
						{
							if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
							{
								SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
							}
							else
							{
								if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
								{
									DeepestParticle = i;
									if (UpdateType == ECollisionUpdateType::Any)
									{
										Contact.Phi = AvgContact.Phi;
										return;
									}
								}
							}
						}
					}
				}
			}
			else
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetAll);
				if (Object.GetType(true) == ImplicitObjectType::LevelSet && NumParticles > 0)
				{
					const TLevelSet<float, 3>* LevelSet = Object.GetObject<Chaos::TLevelSet<float, 3>>();
					const TUniformGrid<float, 3>& Grid = LevelSet->GetGrid();

					if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
					{
						ispc::SampleLevelSetNormalAverageAll(
							(ispc::FVector&)Grid.MinCorner(),
							(ispc::FVector&)Grid.MaxCorner(),
							(ispc::FVector&)Grid.Dx(),
							(ispc::FIntVector&)Grid.Counts(),
							(ispc::TArrayND*)&LevelSet->GetPhiArray(),
							(ispc::FTransform&)SampleToObjectTM,
							(ispc::FVector*)&SampleParticles.XArray()[0],
							Thickness,
							TotalThickness,
							(ispc::FVector&)AvgContact.Location,
							NumParticles);
					}
					else
					{
						ispc::SampleLevelSetNoNormalAll(
							(ispc::FVector&)Grid.MinCorner(),
							(ispc::FVector&)Grid.MaxCorner(),
							(ispc::FVector&)Grid.Dx(),
							(ispc::FIntVector&)Grid.Counts(),
							(ispc::TArrayND*)&LevelSet->GetPhiArray(),
							(ispc::FTransform&)SampleToObjectTM,
							(ispc::FVector*)&SampleParticles.XArray()[0],
							DeepestParticle,
							AvgContact.Phi,
							NumParticles);

						if (UpdateType == ECollisionUpdateType::Any)
						{
							Contact.Phi = AvgContact.Phi;
							return;
						}
					}
				}
				else if (Object.GetType(true) == ImplicitObjectType::Plane && NumParticles > 0)
				{
					const TPlane<float, 3>* Plane = Object.GetObject<Chaos::TPlane<float, 3>>();

					if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
					{
						ispc::SamplePlaneNormalAverageAll(
							(ispc::FVector&)Plane->Normal(),
							(ispc::FVector&)Plane->X(),
							(ispc::FTransform&)SampleToObjectTM,
							(ispc::FVector*)&SampleParticles.XArray()[0],
							Thickness,
							TotalThickness,
							(ispc::FVector&)AvgContact.Location,
							NumParticles);
					}
					else
					{
						ispc::SamplePlaneNoNormalAll(
							(ispc::FVector&)Plane->Normal(),
							(ispc::FVector&)Plane->X(),
							(ispc::FTransform&)SampleToObjectTM,
							(ispc::FVector*)&SampleParticles.XArray()[0],
							DeepestParticle,
							AvgContact.Phi,
							NumParticles);

						if (UpdateType == ECollisionUpdateType::Any)
						{
							Contact.Phi = AvgContact.Phi;
							return;
						}
					}
				}
				else if (Object.GetType(true) == ImplicitObjectType::Box && NumParticles > 0)
				{
					const TBox<float, 3>* Box = Object.GetObject<Chaos::TBox<float, 3>>();

					if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
					{
						ispc::SampleBoxNormalAverageAll(
							(ispc::FVector&)Box->Min(),
							(ispc::FVector&)Box->Max(),
							(ispc::FTransform&)SampleToObjectTM,
							(ispc::FVector*)&SampleParticles.XArray()[0],
							Thickness,
							TotalThickness,
							(ispc::FVector&)AvgContact.Location,
							NumParticles);
					}
					else
					{
						ispc::SampleBoxNoNormalAll(
							(ispc::FVector&)Box->Min(),
							(ispc::FVector&)Box->Max(),
							(ispc::FTransform&)SampleToObjectTM,
							(ispc::FVector*)&SampleParticles.XArray()[0],
							DeepestParticle,
							AvgContact.Phi,
							NumParticles);

						if (UpdateType == ECollisionUpdateType::Any)
						{
							Contact.Phi = AvgContact.Phi;
							return;
						}
					}
				}
				else
				{
					//QUICK_SCOPE_CYCLE_COUNTER(STAT_Other);
					for (int32 i = 0; i < NumParticles; ++i)
					{
						if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)
						{
							SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
						}
						else
						{
							if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
							{
								DeepestParticle = i;
								if (UpdateType == ECollisionUpdateType::Any)
								{
									Contact.Phi = AvgContact.Phi;
									return;
								}
							}
						}
					}
				}
			}

			if (NormalAveraging)
			{
				if (TotalThickness < -KINDA_SMALL_NUMBER)
				{
					TVector<float, 3> LocalPoint = AvgContact.Location / TotalThickness;
					TVector<float, 3> LocalNormal;
					const float NewPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
					if (NewPhi < Contact.Phi)
					{
						Contact.Phi = NewPhi;
						Contact.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
						Contact.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
					}
				}
				else
				{
					check(AvgContact.Phi >= Thickness);
				}
			}
			else if (AvgContact.Phi < Contact.Phi)
			{
				check(DeepestParticle >= 0);
				TVector<float, 3> LocalPoint = SampleToObjectTM.TransformPositionNoScale(SampleParticles.X(DeepestParticle));
				TVector<float, 3> LocalNormal;
				Contact.Phi = Object.PhiWithNormal(LocalPoint, LocalNormal);
				Contact.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
				Contact.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
			}
		}
#else		
		template <ECollisionUpdateType UpdateType, typename T, int d>
		void SampleObject(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TBVHParticles<T, d>& SampleParticles, const TRigidTransform<T, d>& SampleParticlesTransform, T Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint)
		{
			SCOPE_CYCLE_COUNTER(STAT_SampleObject);
			TRigidBodyPointContactConstraint<float, 3> AvgConstraint = Constraint;

			TPointContactManifold<float, 3> & Contact = Constraint.Manifold;
			TPointContactManifold<float, 3> & AvgContact = AvgConstraint.Manifold;

			AvgConstraint.Particle[0] = Constraint.Particle[0];
			AvgConstraint.Particle[1] = Constraint.Particle[1];
			AvgContact.Location = TVector<float, 3>::ZeroVector;
			AvgContact.Normal = TVector<float, 3>::ZeroVector;
			AvgContact.Phi = Thickness;
			float TotalThickness = float(0);

			int32 DeepestParticle = -1;
			const int32 NumParticles = SampleParticles.Size();

			const TRigidTransform<T, d> & SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
			if (NumParticles > SampleMinParticlesForAcceleration && Object.HasBoundingBox())
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetPartial);
				TBox<T, d> ImplicitBox = Object.BoundingBox().TransformedBox(ObjectTransform.GetRelativeTransform(SampleParticlesTransform));
				ImplicitBox.Thicken(Thickness);
				TArray<int32> PotentialParticles;
				{
					SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetFindParticles);
					PotentialParticles = SampleParticles.FindAllIntersections(ImplicitBox);
				}
				{
					SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetSignedDistance);
					for (int32 i : PotentialParticles)
					{
						if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
						{
							SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
						}
						else
						{
							if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
							{
								DeepestParticle = i;
								if (UpdateType == ECollisionUpdateType::Any)
								{
									Contact.Phi = AvgContact.Phi;
									return;
								}
							}
						}
					}
				}
			}
			else
			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetAll);
				for (int32 i = 0; i < NumParticles; ++i)
				{
					if (NormalAveraging && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
					{
						const bool bInside = SampleObjectNormalAverageHelper(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
					}
					else
					{
						if (SampleObjectNoNormal(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
						{
							DeepestParticle = i;
							if (UpdateType == ECollisionUpdateType::Any)
							{
								Contact.Phi = AvgContact.Phi;
								return;
							}
						}
					}
				}
			}

			if (NormalAveraging)
			{
				if (TotalThickness < -KINDA_SMALL_NUMBER)
				{
					TVector<T, d> LocalPoint = AvgContact.Location / TotalThickness;
					TVector<T, d> LocalNormal;
					const T NewPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
					if (NewPhi < Contact.Phi)
					{
						Contact.Phi = NewPhi;
						Contact.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
						Contact.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);
					}
				}
				else
				{
					check(AvgContact.Phi >= Thickness);
				}
			}
			else if (AvgContact.Phi < Contact.Phi)
			{
				check(DeepestParticle >= 0);
				TVector<T, d> LocalPoint = SampleToObjectTM.TransformPositionNoScale(SampleParticles.X(DeepestParticle));
				TVector<T, d> LocalNormal;
				Contact.Phi = Object.PhiWithNormal(LocalPoint, LocalNormal);
				Contact.Location = ObjectTransform.TransformPositionNoScale(LocalPoint);
				Contact.Normal = ObjectTransform.TransformVectorNoScale(LocalNormal);

			}
		}
#endif


		DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraints::FindRelevantShapes"), STAT_FindRelevantShapes, STATGROUP_ChaosWide);
		template <typename T, int d>
		TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> FindRelevantShapes(const FImplicitObject* ParticleObj, const TRigidTransform<T, d>& ParticlesTM, const FImplicitObject& LevelsetObj, const TRigidTransform<T, d>& LevelsetTM, const T Thickness)
		{
			SCOPE_CYCLE_COUNTER(STAT_FindRelevantShapes);
			TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> RelevantShapes;
			//find all levelset inner objects
			if (ParticleObj)
			{
				if (ParticleObj->HasBoundingBox())
				{
					const TRigidTransform<T, d> ParticlesToLevelsetTM = ParticlesTM.GetRelativeTransform(LevelsetTM);
					TBox<T, d> ParticleBoundsInLevelset = ParticleObj->BoundingBox().TransformedBox(ParticlesToLevelsetTM);
					ParticleBoundsInLevelset.Thicken(Thickness);
					{
						LevelsetObj.FindAllIntersectingObjects(RelevantShapes, ParticleBoundsInLevelset);
					}
				}
				else
				{
					LevelsetObj.AccumulateAllImplicitObjects(RelevantShapes, TRigidTransform<T, d>::Identity);
				}
			}
			else
			{
				//todo:compute bounds
				LevelsetObj.AccumulateAllImplicitObjects(RelevantShapes, TRigidTransform<T, d>::Identity);
			}

			return RelevantShapes;
		}

		template TRigidTransform<float, 3> GetTransform(const TGeometryParticleHandle<float, 3>* Particle);
		template PMatrix<float, 3, 3> ComputeFactorMatrix3(const TVector<float, 3>& V, const PMatrix<float, 3, 3>& M, const float& Im);
		template TVector<float, 3> GetEnergyClampedImpulse(const TCollisionConstraintBase<float, 3>& Constraint, const TVector<float, 3>& Impulse, const TVector<float, 3>& VectorToPoint1, const TVector<float, 3>& VectorToPoint2, const TVector<float, 3>& Velocity1, const TVector<float, 3>& Velocity2);
		template bool SampleObjectHelper<float, 3>(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TRigidTransform<float, 3>& SampleToObjectTransform, const TVector<float, 3>& SampleParticle, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template bool SampleObjectNoNormal(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TRigidTransform<float, 3>& SampleToObjectTransform, const TVector<float, 3>& SampleParticle, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template bool SampleObjectNormalAverageHelper(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TRigidTransform<float, 3>& SampleToObjectTransform, const TVector<float, 3>& SampleParticle, float Thickness, float& TotalThickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
#if INTEL_ISPC
		template void SampleObject<ECollisionUpdateType::Any>(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void SampleObject<ECollisionUpdateType::Deepest>(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
#else
		template void SampleObject<ECollisionUpdateType::Any, float, 3>(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
		template void SampleObject<ECollisionUpdateType::Deepest, float, 3>(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
#endif
		template TArray<Pair<const FImplicitObject*, TRigidTransform<float, 3>>> FindRelevantShapes<float, 3>(const FImplicitObject* ParticleObj, const TRigidTransform<float, 3>& ParticlesTM, const FImplicitObject& LevelsetObj, const TRigidTransform<float, 3>& LevelsetTM, const float Thickness);

	} // Collisions

} // Chaos
