// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionResolutionAlgo.h"

#include "Chaos/ChaosPerfTest.h"
#include "Chaos/CollisionResolutionConvexConvex.h"
#include "Chaos/Defines.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/ISpatialAccelerationCollection.h"
#include "Chaos/Levelset.h"
#include "Chaos/Pair.h"
#include "Chaos/Sphere.h"
#include "Chaos/Capsule.h"
#include "Chaos/Transform.h"
#include "ChaosLog.h"
#include "ChaosStats.h"
#include "Stats/Stats.h" 

#if INTEL_ISPC
#include "PBDCollisionConstraint.ispc.generated.h"
#endif

namespace Chaos
{
	template<typename T, int d>
	void ConstructLevelsetConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{

		if (!Particle1->Geometry() || (Particle0->AsDynamic() && !Particle0->AsDynamic()->CollisionParticlesSize() && Particle0->Geometry() && !Particle0->Geometry()->IsUnderlyingUnion()))
		{
			Constraint.Particle[0] = Particle1;
			Constraint.Particle[1] = Particle0;
			Constraint.AddManifold(Implicit1, Implicit0);
		}
		else
		{
			Constraint.Particle[0] = Particle0;
			Constraint.Particle[1] = Particle1;
			Constraint.AddManifold(Implicit0, Implicit1);
		}
	}

	template<typename T, int d>
	void ConstructBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<typename T, int d>
	void ConstructBoxPlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<typename T, int d>
	void ConstructSphereConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<typename T, int d>
	void ConstructSpherePlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<typename T, int d>
	void ConstructSphereBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<typename T, int d>
	void ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<typename T, int d>
	void ConstructCapsuleBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	template<typename T, int d>
	void ConstructSingleUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		Constraint.Particle[0] = Particle0;
		Constraint.Particle[1] = Particle1;
		Constraint.AddManifold(Implicit0, Implicit1);
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateUnionUnionConstraint"), STAT_UpdateUnionUnionConstraint, STATGROUP_ChaosWide);

	template<typename T, int d>
	void ConstructUnionUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		//SCOPE_CYCLE_COUNTER(STAT_UpdateUnionUnionConstraint);

		const FImplicitObject* ParticleObj = Particle0->Geometry().Get();
		const FImplicitObject* LevelsetObj = Particle1->Geometry().Get();

		TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->X(), Particle0->R());
		TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->X(), Particle1->R());

		const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes2(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

		for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
		{
			const FImplicitObject* LevelsetInnerObj = LevelsetObjPair.First;
			const TRigidTransform<T, d>& LevelsetInnerObjTM = LevelsetObjPair.Second * LevelsetTM;

			//now find all particle inner objects
			const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes2(LevelsetInnerObj, LevelsetInnerObjTM, *ParticleObj, ParticlesTM, Thickness);

			//for each inner obj pair, update constraint
			for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticlePair : ParticleShapes)
			{
				const FImplicitObject* ParticleInnerObj = ParticlePair.First;
				ConstructConstraintsImpl<T, d>(Particle0, Particle1, ParticleInnerObj, LevelsetInnerObj, Thickness, Constraint);
			}
		}
	}

	template<typename T, int d>
	void ConstructPairConstraintImpl(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		// See if we already have a constraint for this shape pair
		if (Constraint.ContainsManifold(Implicit0, Implicit1))
		{
			return;
		}

		if (!Implicit0 || !Implicit1)
		{
			ConstructLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		if (Implicit0->GetType() == TBox<T, d>::StaticType() && Implicit1->GetType() == TBox<T, d>::StaticType())
		{
			ConstructBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TSphere<T, d>::StaticType() && Implicit1->GetType() == TSphere<T, d>::StaticType())
		{
			ConstructSphereConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TBox<T, d>::StaticType() && Implicit1->GetType() == TPlane<T, d>::StaticType())
		{
			ConstructBoxPlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit1->GetType() == TBox<T, d>::StaticType() && Implicit0->GetType() == TPlane<T, d>::StaticType())
		{
			ConstructBoxPlaneConstraints(Particle1, Particle0, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TSphere<T, d>::StaticType() && Implicit1->GetType() == TPlane<T, d>::StaticType())
		{
			ConstructSpherePlaneConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit1->GetType() == TSphere<T, d>::StaticType() && Implicit0->GetType() == TPlane<T, d>::StaticType())
		{
			ConstructSpherePlaneConstraints(Particle1, Particle0, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TSphere<T, d>::StaticType() && Implicit1->GetType() == TBox<T, d>::StaticType())
		{
			ConstructSphereBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit1->GetType() == TSphere<T, d>::StaticType() && Implicit0->GetType() == TBox<T, d>::StaticType())
		{
			ConstructSphereBoxConstraints(Particle1, Particle0, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TCapsule<T>::StaticType() && Implicit1->GetType() == TCapsule<T>::StaticType())
		{
			ConstructCapsuleCapsuleConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TCapsule<T>::StaticType() && Implicit1->GetType() == TBox<T, d>::StaticType())
		{
			ConstructCapsuleBoxConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit1->GetType() == TCapsule<T>::StaticType() && Implicit0->GetType() == TBox<T, d>::StaticType())
		{
			ConstructCapsuleBoxConstraints(Particle1, Particle0, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() < TImplicitObjectUnion<T, d>::StaticType() && Implicit1->GetType() == TImplicitObjectUnion<T, d>::StaticType())
		{
			ConstructSingleUnionConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TImplicitObjectUnion<T, d>::StaticType() && Implicit1->GetType() < TImplicitObjectUnion<T, d>::StaticType())
		{
			ConstructSingleUnionConstraints(Particle1, Particle0, Implicit0, Implicit1, Thickness, Constraint);
		}
		else if (Implicit0->GetType() == TImplicitObjectUnion<T, d>::StaticType() && Implicit1->GetType() == TImplicitObjectUnion<T, d>::StaticType())
		{
			// Union-union creates multiple manifolds - we should never get here for this pair type. See ConstructConstraintsImpl and ConstructUnionUnionConstraints
			ensure(false);
		}
		else if (Implicit0->IsConvex() && Implicit1->IsConvex())
		{
			CollisionResolutionConvexConvex<T, d>::ConstructConvexConvexConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else
		{
			ConstructLevelsetConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
	}

	template <typename T, int d>
	bool SampleObjectHelper2(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
		TVector<T, d> LocalNormal;
		T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);

		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;
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
	bool SampleObjectNoNormal2(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
		TVector<T, d> LocalNormal;
		T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);

		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;
		if (LocalPhi < Contact.Phi)
		{
			Contact.Phi = LocalPhi;
			return true;
		}
		return false;
	}

	template <typename T, int d>
	bool SampleObjectNormalAverageHelper2(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, T& TotalThickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TVector<T, d> LocalPoint = SampleToObjectTransform.TransformPositionNoScale(SampleParticle);
		TVector<T, d> LocalNormal;
		T LocalPhi = Object.PhiWithNormal(LocalPoint, LocalNormal);
		T LocalThickness = LocalPhi - Thickness;

		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;
		if (LocalThickness < -KINDA_SMALL_NUMBER)
		{
			Contact.Location += LocalPoint * LocalThickness;
			TotalThickness += LocalThickness;
			return true;
		}
		return false;
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateLevelsetPartial"), STAT_UpdateLevelsetPartial, STATGROUP_ChaosWide);
	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateLevelsetFindParticles"), STAT_UpdateLevelsetFindParticles, STATGROUP_ChaosWide);
	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateLevelsetBVHTraversal"), STAT_UpdateLevelsetBVHTraversal, STATGROUP_ChaosWide);
	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateLevelsetSignedDistance"), STAT_UpdateLevelsetSignedDistance, STATGROUP_ChaosWide);
	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateLevelsetAll"), STAT_UpdateLevelsetAll, STATGROUP_ChaosWide);
	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::SampleObject"), STAT_SampleObject, STATGROUP_ChaosWide);

	int32 NormalAveraging2 = 1;
	FAutoConsoleVariableRef CVarNormalAveraging2(TEXT("p.NormalAveraging2"), NormalAveraging2, TEXT(""));

	int32 SampleMinParticlesForAcceleration2 = 2048;
	FAutoConsoleVariableRef CVarSampleMinParticlesForAcceleration2(TEXT("p.SampleMinParticlesForAcceleration2"), SampleMinParticlesForAcceleration2, TEXT("The minimum number of particles needed before using an acceleration structure when sampling"));


	template <ECollisionUpdateType UpdateType, typename T, int d>
	void SampleObject2(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TBVHParticles<T, d>& SampleParticles, const TRigidTransform<T, d>& SampleParticlesTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_SampleObject);
		TRigidBodyContactConstraint<float, 3> AvgConstraint = Constraint;

		TContactData<float, 3> & Contact = Constraint.ShapeManifold.Manifold;
		TContactData<float, 3> & AvgContact = AvgConstraint.ShapeManifold.Manifold;

		AvgConstraint.Particle[0] = Constraint.Particle[0];
		AvgConstraint.Particle[1] = Constraint.Particle[1];
		AvgContact.Location = TVector<float, 3>::ZeroVector;
		AvgContact.Normal = TVector<float, 3>::ZeroVector;
		AvgContact.Phi = Thickness;
		float TotalThickness = float(0);

		int32 DeepestParticle = -1;
		const int32 NumParticles = SampleParticles.Size();

		const TRigidTransform<T, d> & SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
		if (NumParticles > SampleMinParticlesForAcceleration2 && Object.HasBoundingBox())
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
					if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
					{
						SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
					}
					else
					{
						if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
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
				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)	//if we just want one don't bother with normal
				{
					const bool bInside = SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
				}
				else
				{
					if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
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

		if (NormalAveraging2)
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


#if INTEL_ISPC
	template<ECollisionUpdateType UpdateType>
	void SampleObject2(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_SampleObject);
		TRigidBodyContactConstraint<float, 3> AvgConstraint = Constraint;

		TContactData<float, 3> & Contact = Constraint.ShapeManifold.Manifold;
		TContactData<float, 3> & AvgContact = AvgConstraint.ShapeManifold.Manifold;

		AvgConstraint.Particle[0] = Constraint.Particle[0];
		AvgConstraint.Particle[1] = Constraint.Particle[1];
		AvgContact.Location = TVector<float, 3>::ZeroVector;
		AvgContact.Normal = TVector<float, 3>::ZeroVector;
		AvgContact.Phi = Thickness;
		float TotalThickness = float(0);

		int32 DeepestParticle = -1;

		const TRigidTransform<float, 3>& SampleToObjectTM = SampleParticlesTransform.GetRelativeTransform(ObjectTransform);
		int32 NumParticles = SampleParticles.Size();

		if (NumParticles > SampleMinParticlesForAcceleration2 && Object.HasBoundingBox())
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
					const TLevelSet<float, 3>* LevelSet = Object.GetObject<Chaos::TLevelSet<float, 3>>();
					const TUniformGrid<float, 3>& Grid = LevelSet->GetGrid();

					if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
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

					if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
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
						if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
						{
							SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
						}
						else
						{
							if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
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

				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
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

				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
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

				if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
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
					if (NormalAveraging2 && UpdateType != ECollisionUpdateType::Any)
					{
						SampleObjectNormalAverageHelper2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, TotalThickness, AvgConstraint);
					}
					else
					{
						if (SampleObjectNoNormal2(Object, ObjectTransform, SampleToObjectTM, SampleParticles.X(i), Thickness, AvgConstraint))
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

		if (NormalAveraging2)
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
#endif

	template<ECollisionUpdateType UpdateType, typename T, int d>
	void UpdateConstraintImp(const FImplicitObject& ParticleObject, const TRigidTransform<T, d>& ParticleTM, const FImplicitObject& LevelsetObject, const TRigidTransform<T, d>& LevelsetTM, const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		if (ParticleObject.GetType() == TBox<T, d>::StaticType() && LevelsetObject.GetType() == TBox<T, d>::StaticType())
		{
			UpdateBoxConstraint(*ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TSphere<T, d>::StaticType() && LevelsetObject.GetType() == TSphere<T, d>::StaticType())
		{
			UpdateSphereConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TBox<T, d>::StaticType() && LevelsetObject.GetType() == TPlane<T, d>::StaticType())
		{
			UpdateBoxPlaneConstraint(*ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TPlane<T, d>>(), LevelsetTM, Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TSphere<T, d>::StaticType() && LevelsetObject.GetType() == TPlane<T, d>::StaticType())
		{
			UpdateSpherePlaneConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TPlane<T, d>>(), LevelsetTM, Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TSphere<T, d>::StaticType() && LevelsetObject.GetType() == TBox<T, d>::StaticType())
		{
			UpdateSphereBoxConstraint(*ParticleObject.template GetObject<TSphere<T, d>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TCapsule<T>::StaticType() && LevelsetObject.GetType() == TCapsule<T>::StaticType())
		{
			UpdateCapsuleCapsuleConstraint(*ParticleObject.template GetObject<TCapsule<T>>(), ParticleTM, *LevelsetObject.template GetObject<TCapsule<T>>(), LevelsetTM, Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TCapsule<T>::StaticType() && LevelsetObject.GetType() == TBox<T, d>::StaticType())
		{
			UpdateCapsuleBoxConstraint(*ParticleObject.template GetObject<TCapsule<T>>(), ParticleTM, *LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TPlane<T, d>::StaticType() && LevelsetObject.GetType() == TBox<T, d>::StaticType())
		{
			TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
			UpdateBoxPlaneConstraint(*LevelsetObject.template GetObject<TBox<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TPlane<T, d>>(), ParticleTM, Thickness, TmpConstraint);
			if (TmpConstraint.GetPhi() < Constraint.GetPhi())
			{
				Constraint = TmpConstraint;
				Constraint.SetNormal(-Constraint.GetNormal());
			}
		}
		else if (ParticleObject.GetType() == TPlane<T, d>::StaticType() && LevelsetObject.GetType() == TSphere<T, d>::StaticType())
		{
			TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
			UpdateSpherePlaneConstraint(*LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TPlane<T, d>>(), ParticleTM, Thickness, TmpConstraint);
			if (TmpConstraint.GetPhi() < Constraint.GetPhi())
			{
				Constraint = TmpConstraint;
				Constraint.SetNormal(-Constraint.GetNormal());
			}
		}
		else if (ParticleObject.GetType() == TBox<T, d>::StaticType() && LevelsetObject.GetType() == TSphere<T, d>::StaticType())
		{
			TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
			UpdateSphereBoxConstraint(*LevelsetObject.template GetObject<TSphere<T, d>>(), LevelsetTM, *ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, Thickness, TmpConstraint);
			if (TmpConstraint.GetPhi() < Constraint.GetPhi())
			{
				Constraint = TmpConstraint;
				Constraint.SetNormal(-Constraint.GetNormal());
			}
		}
		else if (ParticleObject.GetType() == TBox<T, d>::StaticType() && LevelsetObject.GetType() == TCapsule<T>::StaticType())
		{
			TRigidBodyContactConstraint<T, d> TmpConstraint = Constraint;
			UpdateCapsuleBoxConstraint(*LevelsetObject.template GetObject<TCapsule<T>>(), LevelsetTM, *ParticleObject.template GetObject<TBox<T, d>>(), ParticleTM, Thickness, TmpConstraint);
			if (TmpConstraint.GetPhi() < Constraint.GetPhi())
			{
				Constraint = TmpConstraint;
				Constraint.SetNormal(-Constraint.GetNormal());
			}
		}
		else if (ParticleObject.GetType() < TImplicitObjectUnion<T, d>::StaticType() && LevelsetObject.GetType() == TImplicitObjectUnion<T, d>::StaticType())
		{
			return UpdateSingleUnionConstraint<UpdateType>(Thickness, Constraint);
		}
		else if (ParticleObject.GetType() == TImplicitObjectUnion<T, d>::StaticType() && LevelsetObject.GetType() < TImplicitObjectUnion<T, d>::StaticType())
		{
			check(false);	//should not be possible to get this ordering (see ComputeConstraint)
		}
		else if (ParticleObject.GetType() == TImplicitObjectUnion<T, d>::StaticType() && LevelsetObject.GetType() == TImplicitObjectUnion<T, d>::StaticType())
		{
			return UpdateUnionUnionConstraint<UpdateType>(Thickness, Constraint);
		}
		else if (ParticleObject.IsConvex() && LevelsetObject.IsConvex())
		{
			CollisionResolutionConvexConvex<T, d>::UpdateConvexConvexConstraint(ParticleObject, ParticleTM, LevelsetObject, LevelsetTM, Thickness, Constraint);
		}
		else if (LevelsetObject.IsUnderlyingUnion())
		{
			UpdateUnionLevelsetConstraint<UpdateType>(Thickness, Constraint);
		}
		else if (ParticleObject.IsUnderlyingUnion())
		{
			UpdateLevelsetUnionConstraint<UpdateType>(Thickness, Constraint);
		}
		else
		{
			UpdateLevelsetConstraint<UpdateType>(Thickness, Constraint);
		}
	}

	template<typename T, int d>
	void ConstructConstraintsImpl(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodyContactConstraint<T, d> & Constraint)
	{
		// TriangleMesh implicits are for scene query only.
		if (Implicit0 && GetInnerType(Implicit0->GetType()) == ImplicitObjectType::TriangleMesh) return;
		if (Implicit1 && GetInnerType(Implicit1->GetType()) == ImplicitObjectType::TriangleMesh) return;

		if (Implicit0 && Implicit0->GetType() == TImplicitObjectUnion<T, d>::StaticType() &&
			Implicit1 && Implicit1->GetType() == TImplicitObjectUnion<T, d>::StaticType())
		{
			ConstructUnionUnionConstraints(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
		else
		{
			ConstructPairConstraintImpl(Particle0, Particle1, Implicit0, Implicit1, Thickness, Constraint);
		}
	}

	template <typename T, int d>
	bool UpdateBoxPlaneConstraint(const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;

#if USING_CODE_ANALYSIS
		MSVC_PRAGMA(warning(push))
			MSVC_PRAGMA(warning(disable : ALL_CODE_ANALYSIS_WARNINGS))
#endif	// USING_CODE_ANALYSIS

			bool bApplied = false;
		const TRigidTransform<T, d> BoxToPlaneTransform(BoxTransform.GetRelativeTransform(PlaneTransform));
		const TVector<T, d> Extents = Box.Extents();
		constexpr int32 NumCorners = 2 + 2 * d;
		constexpr T Epsilon = KINDA_SMALL_NUMBER;

		TVector<T, d> Corners[NumCorners];
		int32 CornerIdx = 0;
		Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max());
		Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min());
		for (int32 j = 0; j < d; ++j)
		{
			Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Min() + TVector<T, d>::AxisVector(j) * Extents);
			Corners[CornerIdx++] = BoxToPlaneTransform.TransformPosition(Box.Max() - TVector<T, d>::AxisVector(j) * Extents);
		}

#if USING_CODE_ANALYSIS
		MSVC_PRAGMA(warning(pop))
#endif	// USING_CODE_ANALYSIS

			TVector<T, d> PotentialConstraints[NumCorners];
		int32 NumConstraints = 0;
		for (int32 i = 0; i < NumCorners; ++i)
		{
			TVector<T, d> Normal;
			const T NewPhi = Plane.PhiWithNormal(Corners[i], Normal);
			if (NewPhi < Contact.Phi + Epsilon)
			{
				if (NewPhi <= Contact.Phi - Epsilon)
				{
					NumConstraints = 0;
				}
				Contact.Phi = NewPhi;
				Contact.Normal = PlaneTransform.TransformVector(Normal);
				Contact.Location = PlaneTransform.TransformPosition(Corners[i]);
				PotentialConstraints[NumConstraints++] = Contact.Location;
				bApplied = true;
			}
		}
		if (NumConstraints > 1)
		{
			TVector<T, d> AverageLocation(0);
			for (int32 ConstraintIdx = 0; ConstraintIdx < NumConstraints; ++ConstraintIdx)
			{
				AverageLocation += PotentialConstraints[ConstraintIdx];
			}
			Contact.Location = AverageLocation / NumConstraints;
		}

		return bApplied;
	}

	template <typename T, int d>
	void UpdateSphereConstraint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;

		const TVector<T, d> Center1 = Sphere1Transform.TransformPosition(Sphere1.GetCenter());
		const TVector<T, d> Center2 = Sphere2Transform.TransformPosition(Sphere2.GetCenter());
		const TVector<T, d> Direction = Center1 - Center2;
		const T Size = Direction.Size();
		const T NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());
		if (NewPhi < Contact.Phi)
		{
			Contact.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
			Contact.Phi = NewPhi;
			Contact.Location = Center1 - Sphere1.GetRadius() * Contact.Normal;
		}
	}

	template <typename T, int d>
	void UpdateSpherePlaneConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;

		const TRigidTransform<T, d> SphereToPlaneTransform(PlaneTransform.Inverse() * SphereTransform);
		const TVector<T, d> SphereCenter = SphereToPlaneTransform.TransformPosition(Sphere.GetCenter());

		TVector<T, d> NewNormal;
		T NewPhi = Plane.PhiWithNormal(SphereCenter, NewNormal);
		NewPhi -= Sphere.GetRadius();

		if (NewPhi < Contact.Phi)
		{
			Contact.Phi = NewPhi;
			Contact.Normal = PlaneTransform.TransformVectorNoScale(NewNormal);
			Contact.Location = SphereCenter - Contact.Normal * Sphere.GetRadius();
		}
	}

	template <typename T, int d>
	void UpdateSphereBoxConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;

		const TRigidTransform<T, d> SphereToBoxTransform(SphereTransform * BoxTransform.Inverse());
		const TVector<T, d> SphereCenterInBox = SphereToBoxTransform.TransformPosition(Sphere.GetCenter());

		TVector<T, d> NewNormal;
		T NewPhi = Box.PhiWithNormal(SphereCenterInBox, NewNormal);
		NewPhi -= Sphere.GetRadius();

		if (NewPhi < Contact.Phi)
		{
			Contact.Phi = NewPhi;
			Contact.Normal = BoxTransform.TransformVectorNoScale(NewNormal);
			Contact.Location = SphereTransform.TransformPosition(Sphere.GetCenter()) - Contact.Normal * Sphere.GetRadius();
		}
	}

	template <typename T, int d>
	void UpdateCapsuleCapsuleConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;

		FVector A1 = ATransform.TransformPosition(A.GetX1());
		FVector A2 = ATransform.TransformPosition(A.GetX2());
		FVector B1 = BTransform.TransformPosition(B.GetX1());
		FVector B2 = BTransform.TransformPosition(B.GetX2());
		FVector P1, P2;
		FMath::SegmentDistToSegmentSafe(A1, A2, B1, B2, P1, P2);

		TVector<T, d> Delta = P2 - P1;
		T DeltaLen = Delta.Size();
		if (DeltaLen > KINDA_SMALL_NUMBER)
		{
			T NewPhi = DeltaLen - (A.GetRadius() + B.GetRadius());
			if (NewPhi < Contact.Phi)
			{
				TVector<T, d> Dir = Delta / DeltaLen;
				Contact.Phi = NewPhi;
				Contact.Normal = -Dir;
				Contact.Location = P1 + Dir * A.GetRadius();
			}
		}
	}

	template <typename T, int d>
	void UpdateCapsuleBoxConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TBox<T, d>& B, const TRigidTransform<T, d>& BTransform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		// @todo(ccaulfield): Add custom capsule-box collision
		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;

		TRigidTransform<T, d> BToATransform = BTransform.GetRelativeTransform(ATransform);

		// Use GJK to track closest points (not strictly necessary yet)
		TVector<T, d> NearPointALocal, NearPointBLocal;
		T NearPointDistance;
		if (GJKDistance<T>(A, B, BToATransform, NearPointDistance, NearPointALocal, NearPointBLocal))
		{
			TVector<T, d> NearPointAWorld = ATransform.TransformPosition(NearPointALocal);
			TVector<T, d> NearPointBWorld = BTransform.TransformPosition(NearPointBLocal);
			TVector<T, d> NearPointBtoAWorld = NearPointAWorld - NearPointBWorld;
			Contact.Phi = NearPointDistance;
			Contact.Normal = NearPointBtoAWorld.GetSafeNormal();
			Contact.Location = NearPointAWorld;
		}
		else
		{
			// Use box particle samples against the implicit capsule
			const TArray<TVector<T, d>> BParticles = B.ComputeSamplePoints();
			const int32 NumParticles = BParticles.Num();
			for (int32 ParticleIndex = 0; ParticleIndex < NumParticles; ++ParticleIndex)
			{
				if (SampleObjectHelper2(A, ATransform, BToATransform, BParticles[ParticleIndex], Thickness, Constraint))
				{
					// SampleObjectHelper2 expects A to be the box, so reverse the results
					Contact.Normal = -Contact.Normal;
				}
			}
		}

	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::FindRelevantShapes"), STAT_FindRelevantShapes, STATGROUP_ChaosWide);
	template <typename T, int d>
	TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> FindRelevantShapes2(const FImplicitObject* ParticleObj, const TRigidTransform<T, d>& ParticlesTM, const FImplicitObject& LevelsetObj, const TRigidTransform<T, d>& LevelsetTM, const T Thickness)
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

	template<ECollisionUpdateType UpdateType, typename T, int d>
	void UpdateUnionUnionConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateUnionUnionConstraint);

		TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
		TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];

		TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
		TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

		const FImplicitObject* ParticleObj = Particle0->Geometry().Get();
		const FImplicitObject* LevelsetObj = Particle1->Geometry().Get();
		const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes2(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

		for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
		{
			const FImplicitObject& LevelsetInnerObj = *LevelsetObjPair.First;
			const TRigidTransform<T, d>& LevelsetInnerObjTM = LevelsetObjPair.Second * LevelsetTM;

			//now find all particle inner objects
			const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes2(&LevelsetInnerObj, LevelsetInnerObjTM, *ParticleObj, ParticlesTM, Thickness);

			//for each inner obj pair, update constraint
			for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticlePair : ParticleShapes)
			{
				const FImplicitObject& ParticleInnerObj = *ParticlePair.First;
				const TRigidTransform<T, d> ParticleInnerObjTM = ParticlePair.Second * ParticlesTM;
				UpdateConstraintImp<UpdateType>(ParticleInnerObj, ParticleInnerObjTM, LevelsetInnerObj, LevelsetInnerObjTM, Thickness, Constraint);
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateSingleUnionConstraint"), STAT_UpdateSingleUnionConstraint, STATGROUP_ChaosWide);
	template<ECollisionUpdateType UpdateType, typename T, int d>
	void UpdateSingleUnionConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateSingleUnionConstraint);

		TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
		TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];

		TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
		TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

		const FImplicitObject* ParticleObj = Particle0->Geometry().Get();
		const FImplicitObject* LevelsetObj = Particle1->Geometry().Get();
		const TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes2(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

		for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
		{
			const FImplicitObject& LevelsetInnerObj = *LevelsetObjPair.First;
			const TRigidTransform<T, d> LevelsetInnerObjTM = LevelsetObjPair.Second * LevelsetTM;
			UpdateConstraintImp<UpdateType>(*ParticleObj, ParticlesTM, LevelsetInnerObj, LevelsetInnerObjTM, Thickness, Constraint);
		}
	}



	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateUnionLevelsetConstraint"), STAT_UpdateUnionLevelsetConstraint, STATGROUP_ChaosWide);
	template<ECollisionUpdateType UpdateType, typename T, int d>
	void UpdateUnionLevelsetConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateUnionLevelsetConstraint);

		TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
		TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];

		TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
		TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

		if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
		{
			return;
		}

		if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
		{
			return;
		}

		const FImplicitObject* ParticleObj = Particle0->Geometry().Get();
		const FImplicitObject* LevelsetObj = Particle1->Geometry().Get();
		TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> LevelsetShapes = FindRelevantShapes2(ParticleObj, ParticlesTM, *LevelsetObj, LevelsetTM, Thickness);

		if (LevelsetShapes.Num() && Particle0->CollisionParticles().Get())
		{
			const TBVHParticles<T, d>& SampleParticles = *Particle0->CollisionParticles().Get();
			if (SampleParticles.Size())
			{
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
				{
					const FImplicitObject* Object = LevelsetObjPair.First;
					const TRigidTransform<T, d> ObjectTM = LevelsetObjPair.Second * LevelsetTM;
					SampleObject2<UpdateType>(*Object, ObjectTM, SampleParticles, ParticlesTM, Thickness, Constraint);
					if (UpdateType == ECollisionUpdateType::Any && Constraint.GetPhi() < Thickness)
					{
						return;
					}
				}
			}
#if CHAOS_PARTICLEHANDLE_TODO
			else if (ParticleObj && ParticleObj->IsUnderlyingUnion())
			{
				const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
				//need to traverse shapes to get their collision particles
				for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& LevelsetObjPair : LevelsetShapes)
				{
					const FImplicitObject* LevelsetInnerObject = LevelsetObjPair.First;
					const TRigidTransform<T, d> LevelsetInnerObjectTM = LevelsetObjPair.Second * LevelsetTM;

					TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes2(LevelsetInnerObject, LevelsetInnerObjectTM, *ParticleObj, ParticlesTM, Thickness);
					for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
					{
						const FImplicitObject* ParticleInnerObject = ParticleObjPair.First;
						const TRigidTransform<T, d> ParticleInnerObjectTM = ParticleObjPair.Second * ParticlesTM;

						if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(ParticleInnerObject))
						{
							const TBVHParticles<T, d>& InnerSampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
							SampleObject2<UpdateType>(*LevelsetInnerObject, LevelsetInnerObjectTM, InnerSampleParticles, ParticleInnerObjectTM, Thickness, Constraint);
							if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
							{
								return;
							}
						}
					}

				}
			}
#endif
		}
	}


	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateLevelsetUnionConstraint"), STAT_UpdateLevelsetUnionConstraint, STATGROUP_ChaosWide);
	template<ECollisionUpdateType UpdateType, typename T, int d>
	void UpdateLevelsetUnionConstraint(const T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetUnionConstraint);

		TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
		TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];

		TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
		TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());

		const FImplicitObject* ParticleObj = Particle0->Geometry().Get();
		const FImplicitObject* LevelsetObj = Particle1->Geometry().Get();

		if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
		{
			return;
		}

		if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
		{
			return;
		}

#if CHAOS_PARTICLEHANDLE_TODO
		TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> ParticleShapes = FindRelevantShapes2(LevelsetObj, LevelsetTM, *ParticleObj, ParticlesTM, Thickness);
		check(ParticleObj->IsUnderlyingUnion());
		const TImplicitObjectUnion<T, d>* UnionObj = static_cast<const TImplicitObjectUnion<T, d>*>(ParticleObj);
		for (const Pair<const FImplicitObject*, TRigidTransform<T, d>>& ParticleObjPair : ParticleShapes)
		{
			const FImplicitObject* Object = ParticleObjPair.First;

			if (const int32* OriginalIdx = UnionObj->MCollisionParticleLookupHack.Find(Object))
			{
				const TBVHParticles<T, d>& SampleParticles = *InParticles.CollisionParticles(*OriginalIdx).Get();
				const TRigidTransform<T, d> ObjectTM = ParticleObjPair.Second * ParticlesTM;

				SampleObject2<UpdateType>(*LevelsetObj, LevelsetTM, SampleParticles, ObjectTM, Thickness, Constraint);
				if (UpdateType == ECollisionUpdateType::Any && Constraint.Phi < Thickness)
				{
					return;
				}
			}
		}
#endif
	}


	template <typename T, int d>
	void UpdateBoxConstraint(const TBox<T, d>& Box1, const TRigidTransform<T, d>& Box1Transform, const TBox<T, d>& Box2, const TRigidTransform<T, d>& Box2Transform, T Thickness, TRigidBodyContactConstraint<T, d>& Constraint)
	{
		TContactData<T, d> & Contact = Constraint.ShapeManifold.Manifold;

		TBox<T, d> Box2SpaceBox1 = Box1.TransformedBox(Box1Transform * Box2Transform.Inverse());
		TBox<T, d> Box1SpaceBox2 = Box2.TransformedBox(Box2Transform * Box1Transform.Inverse());
		Box2SpaceBox1.Thicken(Thickness);
		Box1SpaceBox2.Thicken(Thickness);
		if (Box1SpaceBox2.Intersects(Box1) && Box2SpaceBox1.Intersects(Box2))
		{
			const TVector<T, d> Box1Center = (Box1Transform * Box2Transform.Inverse()).TransformPosition(Box1.Center());
			bool bDeepOverlap = false;
			if (Box2.SignedDistance(Box1Center) < 0)
			{
				//If Box1 is overlapping Box2 by this much the signed distance approach will fail (box1 gets sucked into box2). In this case just use two spheres
				TSphere<T, d> Sphere1(Box1Transform.TransformPosition(Box1.Center()), Box1.Extents().Min() / 2);
				TSphere<T, d> Sphere2(Box2Transform.TransformPosition(Box2.Center()), Box2.Extents().Min() / 2);
				const TVector<T, d> Direction = Sphere1.GetCenter() - Sphere2.GetCenter();
				T Size = Direction.Size();
				if (Size < (Sphere1.GetRadius() + Sphere2.GetRadius()))
				{
					const T NewPhi = Size - (Sphere1.GetRadius() + Sphere2.GetRadius());;
					if (NewPhi < Contact.Phi)
					{
						bDeepOverlap = true;
						Contact.Normal = Size > SMALL_NUMBER ? Direction / Size : TVector<T, d>(0, 0, 1);
						Contact.Phi = NewPhi;
						Contact.Location = Sphere1.GetCenter() - Sphere1.GetRadius() * Contact.Normal;
					}
				}
			}
			if (!bDeepOverlap || Contact.Phi >= 0)
			{
				//if we didn't have deep penetration use signed distance per particle. If we did have deep penetration but the spheres did not overlap use signed distance per particle

				//UpdateLevelsetConstraintGJK(InParticles, Thickness, Constraint);
				//check(Contact.Phi < MThickness);
				// For now revert to doing all points vs lsv check until we can figure out a good way to get the deepest point without needing this
				{
					const TArray<TVector<T, d>> SampleParticles = Box1.ComputeSamplePoints();
					const TRigidTransform<T, d> Box1ToBox2Transform = Box1Transform.GetRelativeTransform(Box2Transform);
					int32 NumParticles = SampleParticles.Num();
					for (int32 i = 0; i < NumParticles; ++i)
					{
						SampleObjectHelper2(Box2, Box2Transform, Box1ToBox2Transform, SampleParticles[i], Thickness, Constraint);
					}
				}
			}
		}
	}

	DECLARE_CYCLE_STAT(TEXT("TPBDCollisionConstraint::UpdateLevelsetConstraint"), STAT_UpdateLevelsetConstraint, STATGROUP_ChaosWide);
	template<ECollisionUpdateType UpdateType, typename T, int d>
	void UpdateLevelsetConstraint(const T Thickness, TRigidBodyContactConstraint<float, 3>& Constraint)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateLevelsetConstraint);

		TGenericParticleHandle<T, d> Particle0 = Constraint.Particle[0];
		TRigidTransform<T, d> ParticlesTM = TRigidTransform<T, d>(Particle0->P(), Particle0->Q());
		if (!(ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().X)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(ParticlesTM.GetTranslation().Z))))
		{
			return;
		}

		TGenericParticleHandle<T, d> Particle1 = Constraint.Particle[1];
		TRigidTransform<T, d> LevelsetTM = TRigidTransform<T, d>(Particle1->P(), Particle1->Q());
		if (!(ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().X)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Y)) && ensure(!FMath::IsNaN(LevelsetTM.GetTranslation().Z))))
		{
			return;
		}

		const TBVHParticles<T, d>* SampleParticles = nullptr;
		SampleParticles = Particle0->CollisionParticles().Get();

		if (SampleParticles)
		{
			SampleObject2<UpdateType>(*Particle1->Geometry(), LevelsetTM, *SampleParticles, ParticlesTM, Thickness, Constraint);
		}
	}

	template void ConstructConstraintsImpl<float,3>(TGeometryParticleHandle<float, 3>* Particle0, TGeometryParticleHandle<float, 3>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const float Thickness, TRigidBodyContactConstraint<float, 3> & Constraint);
	template void UpdateUnionLevelsetConstraint<ECollisionUpdateType::Any, float, 3>(const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
	template void UpdateUnionLevelsetConstraint<ECollisionUpdateType::Deepest, float, 3>(const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
	template void UpdateConstraintImp<ECollisionUpdateType::Any, float, 3>(const FImplicitObject& ParticleObject, const TRigidTransform<float, 3>& ParticleTM, const FImplicitObject& LevelsetObject, const TRigidTransform<float, 3>& LevelsetTM, const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraints);
	template void UpdateConstraintImp<ECollisionUpdateType::Deepest, float, 3>(const FImplicitObject& ParticleObject, const TRigidTransform<float, 3>& ParticleTM, const FImplicitObject& LevelsetObject, const TRigidTransform<float, 3>& LevelsetTM, const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
	template void UpdateLevelsetConstraint<ECollisionUpdateType::Any, float, 3>(const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);
	template void UpdateLevelsetConstraint<ECollisionUpdateType::Deepest, float, 3>(const float Thickness, TRigidBodyContactConstraint<float, 3>& Constraint);

}
