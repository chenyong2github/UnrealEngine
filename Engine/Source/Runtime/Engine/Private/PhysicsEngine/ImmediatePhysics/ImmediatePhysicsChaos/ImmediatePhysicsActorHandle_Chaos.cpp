// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/MassProperties.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Sphere.h"
#include "Chaos/Utilities.h"

#include "Physics/Experimental/ChaosInterfaceUtils.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"

extern int32 ImmediatePhysicsDisableCollisions;

namespace ImmediatePhysics_Chaos
{
	//
	// Utils
	//

	// @todo(ccaulfield): Max mass conditioning an option (or push it into the constraint which is where it is really needed)
	const FReal Chaos_MaxDimensionRatio = 5.0f;

	Chaos::FMatrix33 CalculateInertia_Solid(const FReal Mass, const FKSphereElem& SphereElem)
	{
		return Chaos::TSphere<FReal, 3>::GetInertiaTensor(Mass, SphereElem.Radius);
	}

	Chaos::FMatrix33 CalculateInertia_Solid(const FReal Mass, const FKSphylElem& SphylElem)
	{
		float Len = SphylElem.Length;
		float Rad = SphylElem.Radius;
		if (Len > Chaos_MaxDimensionRatio * Rad)
		{
			Rad = Len / Chaos_MaxDimensionRatio;
		}
		return Chaos::TCapsule<FReal>::GetInertiaTensor(Mass, Len, Rad);
	}

	Chaos::FMatrix33 CalculateInertia_Solid(const FReal Mass, const FKBoxElem& BoxElem)
	{
		FVector Dim = FVector(BoxElem.X, BoxElem.Y, BoxElem.Z);
		float MaxDim = Dim.GetAbsMax();
		float MinDim = Dim.GetAbsMin();
		if (MaxDim > Chaos_MaxDimensionRatio* MinDim)
		{
			Dim.X = FMath::Lerp(MinDim, MaxDim, (Dim.X - MinDim) / (MaxDim - MinDim));
			Dim.Y = FMath::Lerp(MinDim, MaxDim, (Dim.Y - MinDim) / (MaxDim - MinDim));
			Dim.Z = FMath::Lerp(MinDim, MaxDim, (Dim.Z - MinDim) / (MaxDim - MinDim));
		}
		return Chaos::TBox<FReal, 3>::GetInertiaTensor(Mass, Dim);
	}

	void CalculateMassProperties(const FVector& Scale, const FTransform& LocalTransform, const FKAggregateGeom& AggGeom, Chaos::TMassProperties<FReal, 3>& OutMassProperties)
	{
		using namespace Chaos;
		TArray<TMassProperties<FReal, 3>> AllMassProperties;

		for (uint32 i = 0; i < static_cast<uint32>(AggGeom.SphereElems.Num()); ++i)
		{
			const FKSphereElem ScaledSphereElem = AggGeom.SphereElems[i].GetFinalScaled(Scale, LocalTransform);

			TMassProperties<FReal, 3> MassProperties;
			MassProperties.CenterOfMass = LocalTransform.GetTranslation() + ScaledSphereElem.Center;
			MassProperties.RotationOfMass = FRotation3::FromIdentity();
			MassProperties.Volume = ScaledSphereElem.GetVolume(FVector::OneVector);
			MassProperties.InertiaTensor = CalculateInertia_Solid(MassProperties.Volume, ScaledSphereElem);

			AllMassProperties.Add(MassProperties);
		}
		for (uint32 i = 0; i < static_cast<uint32>(AggGeom.BoxElems.Num()); ++i)
		{
			const auto& BoxElem = AggGeom.BoxElems[i];

			TMassProperties<FReal, 3> MassProperties;
			MassProperties.CenterOfMass = LocalTransform.GetTranslation() + BoxElem.Center;
			MassProperties.RotationOfMass = LocalTransform.GetRotation() * FRotation3(FQuat(BoxElem.Rotation));
			MassProperties.Volume = BoxElem.GetVolume(Scale);
			MassProperties.InertiaTensor = CalculateInertia_Solid(MassProperties.Volume, BoxElem);

			AllMassProperties.Add(MassProperties);
		}
		for (uint32 i = 0; i < static_cast<uint32>(AggGeom.SphylElems.Num()); ++i)
		{
			const FKSphylElem& UnscaledSphyl = AggGeom.SphylElems[i];
			const FKSphylElem ScaledSphylElem = UnscaledSphyl.GetFinalScaled(Scale, LocalTransform);
			float HalfHeight = FMath::Max(ScaledSphylElem.Length * 0.5f, KINDA_SMALL_NUMBER);
			const float Radius = FMath::Max(ScaledSphylElem.Radius, KINDA_SMALL_NUMBER);
			if (HalfHeight < KINDA_SMALL_NUMBER)
			{
				//not a capsule just use a sphere
				const FKSphereElem ScaledSphereElem = FKSphereElem(Radius);

				TMassProperties<FReal, 3> MassProperties;
				MassProperties.CenterOfMass = LocalTransform.GetTranslation() + ScaledSphereElem.Center;
				MassProperties.RotationOfMass = FRotation3::FromIdentity();
				MassProperties.Volume = ScaledSphereElem.GetVolume(FVector::OneVector);
				MassProperties.InertiaTensor = CalculateInertia_Solid(MassProperties.Volume, ScaledSphereElem);

				AllMassProperties.Add(MassProperties);
			}
			else
			{
				TMassProperties<FReal, 3> MassProperties;
				MassProperties.CenterOfMass = LocalTransform.GetTranslation() + ScaledSphylElem.Center;
				MassProperties.RotationOfMass = LocalTransform.GetRotation() * FRotation3(FQuat(ScaledSphylElem.Rotation));
				MassProperties.Volume = ScaledSphylElem.GetVolume(FVector::OneVector);
				MassProperties.InertiaTensor = CalculateInertia_Solid(MassProperties.Volume, ScaledSphylElem);

				AllMassProperties.Add(MassProperties);
			}
		}
#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
		for (uint32 i = 0; i < static_cast<uint32>(AggGeom.ConvexElems.Num()); ++i)
		{
			const FKConvexElem& CollisionBody = AggGeom.ConvexElems[i];
			if (const auto& ConvexImplicit = CollisionBody.GetChaosConvexMesh())
			{
				// @todo(ccaulfield): calculate inertia of convex
			}
		}

		// @todo(ccaulfield): tri meshes...
		//for (const auto& ChaosTriMesh : InParams.ChaosTriMeshes)
		//{
		//}
#endif
		static bool bModeThanOne = false;
		if (AllMassProperties.Num() > 1)
		{
			bModeThanOne = true;
		}

		if (CHAOS_ENSURE(AllMassProperties.Num() > 0))
		{
			OutMassProperties = Combine(AllMassProperties);
		}
		else 
		{
			// @todo : Add support for all types, but for now just hard code a unit sphere tensor {r:50cm} if the type was not processed
			OutMassProperties.CenterOfMass = FVec3(0.0f);
			OutMassProperties.Mass = 523.f;
			OutMassProperties.Volume = 5.24e5;
			OutMassProperties.RotationOfMass = TRotation<FReal, 3>::FromIdentity();
			OutMassProperties.InertiaTensor = PMatrix<FReal, 3, 3>(5.24e5, 5.24e5, 5.24e5);
		}
	}


	bool CreateGeometry(FBodyInstance* BodyInstance, const FVector& Scale, float& OutMass, Chaos::TVector<float, 3>& OutInertia, Chaos::TRigidTransform<float, 3>& OutCoMTransform, TUniquePtr<Chaos::FImplicitObject>& OutGeom, TArray<TUniquePtr<Chaos::TPerShapeData<float, 3>>>& OutShapes)
	{
		using namespace Chaos;

		UBodySetup* BodySetup = BodyInstance->BodySetup.Get();

#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
		TMassProperties<float, 3> MassProperties;
		CalculateMassProperties(Scale, FTransform::Identity, BodySetup->AggGeom, MassProperties);
		float Density = 1.e-3f;	// 1g/cm3	@todo(ccaulfield): should come from material
		if (BodyInstance->bOverrideMass)
		{
			Density = BodyInstance->GetMassOverride() / MassProperties.Volume;
		}
		OutMass = Density * BodyInstance->MassScale * MassProperties.Volume;
		OutInertia = Utilities::ScaleInertia(Density * TVector<float, 3>(MassProperties.InertiaTensor.M[0][0], MassProperties.InertiaTensor.M[1][1], MassProperties.InertiaTensor.M[2][2]), BodyInstance->InertiaTensorScale, true);	// bScaleMass true to match legacy, but not correct
		OutCoMTransform = FTransform(MassProperties.RotationOfMass, MassProperties.CenterOfMass + BodyInstance->COMNudge);
#else
		OutMass = BodyInstance->GetBodyMass();
		OutInertia = BodyInstance->GetBodyInertiaTensor();
		OutCoMTransform = BodyInstance->GetMassSpaceLocal();
#endif


		FBodyCollisionData BodyCollisionData;
		BodyInstance->BuildBodyFilterData(BodyCollisionData.CollisionFilterData);
		BodyInstance->BuildBodyCollisionFlags(BodyCollisionData.CollisionFlags, BodyInstance->GetCollisionEnabled(), BodyInstance->BodySetup->GetCollisionTraceFlag() == CTF_UseComplexAsSimple);

		FGeometryAddParams AddParams;
		AddParams.bDoubleSided = BodySetup->bDoubleSidedGeometry;
		AddParams.CollisionData = BodyCollisionData;
		AddParams.CollisionTraceType = BodySetup->GetCollisionTraceFlag();
		AddParams.Scale = Scale;
		//AddParams.SimpleMaterial = SimpleMaterial;
		//AddParams.ComplexMaterials = TArrayView<UPhysicalMaterial*>(ComplexMaterials);
#if CHAOS_PARTICLE_ACTORTRANSFORM
		AddParams.LocalTransform = FTransform::Identity;
#else
		AddParams.LocalTransform = TRigidTransform<float, 3>(OutCoMTransform.GetRotation().Inverse() * -OutCoMTransform.GetTranslation(), OutCoMTransform.GetRotation().Inverse());
#endif
		AddParams.WorldTransform = BodyInstance->GetUnrealWorldTransform();
		AddParams.Geometry = &BodySetup->AggGeom;
#if WITH_PHYSX
		AddParams.TriMeshes = TArrayView<PxTriangleMesh*>(BodySetup->TriMeshes);
#endif
#if WITH_CHAOS
		AddParams.ChaosTriMeshes = MakeArrayView(BodySetup->ChaosTriMeshes);
#endif

		TArray<TUniquePtr<FImplicitObject>> Geoms;
		TArray<TUniquePtr<TPerShapeData<float, 3>>, TInlineAllocator<1>> Shapes;
		ChaosInterface::CreateGeometry(AddParams, Geoms, Shapes);

		if (Geoms.Num() == 0)
		{
			return false;
		}

		if (Geoms.Num() == 1)
		{
			OutGeom = MoveTemp(Geoms[0]);
		}
		else
		{
			OutGeom = MakeUnique<FImplicitObjectUnion>(MoveTemp(Geoms));
		}

		for (auto& Shape : Shapes)
		{
			OutShapes.Emplace(MoveTemp(Shape));
		}

		return true;
	}

	//
	// Actor Handle
	//

	FActorHandle::FActorHandle(Chaos::TPBDRigidsSOAs<FReal, 3>& InParticles, EActorType ActorType, FBodyInstance* BodyInstance, const FTransform& Transform)
		: Particles(InParticles)
		, ParticleHandle(nullptr)
	{
		using namespace Chaos;

		// @todo(ccaulfield): Scale
		float Mass = 0;
		FVec3 Inertia = FVec3::OneVector;
		FRigidTransform3 CoMTransform = FRigidTransform3::Identity;
		if (CreateGeometry(BodyInstance, FVector::OneVector, Mass, Inertia, CoMTransform, Geometry, Shapes))
		{
			switch (ActorType)
			{
			case EActorType::StaticActor:
				ParticleHandle = Particles.CreateStaticParticles(1, TGeometryParticleParameters<FReal, Dimensions>())[0];
				break;
			case EActorType::KinematicActor:
				ParticleHandle = Particles.CreateKinematicParticles(1, TKinematicGeometryParticleParameters<FReal, Dimensions>())[0];
				break;
			case EActorType::DynamicActor:
				ParticleHandle = Particles.CreateDynamicParticles(1, TPBDRigidParticleParameters<FReal, Dimensions>())[0];
				break;
			}

			if (ParticleHandle != nullptr)
			{
				SetWorldTransform(Transform);

				ParticleHandle->SetGeometry(MakeSerializable(Geometry));

				if (Geometry && Geometry->HasBoundingBox())
				{
					ParticleHandle->SetHasBounds(true);
					ParticleHandle->SetLocalBounds(Geometry->BoundingBox());
					ParticleHandle->SetWorldSpaceInflatedBounds(Geometry->BoundingBox().TransformedAABB(TRigidTransform<float, 3>(ParticleHandle->X(), ParticleHandle->R())));
				}

				if (auto* Kinematic = ParticleHandle->CastToKinematicParticle())
				{
					Kinematic->SetV(FVector::ZeroVector);
					Kinematic->SetW(FVector::ZeroVector);
					Kinematic->SetCenterOfMass(CoMTransform.GetTranslation());
					Kinematic->SetRotationOfMass(CoMTransform.GetRotation());
				}

				auto* Dynamic = ParticleHandle->CastToRigidParticle();
				if (Dynamic && Dynamic->ObjectState() == EObjectStateType::Dynamic)
				{
					float MassInv = (Mass > 0.0f) ? 1.0f / Mass : 0.0f;
					FVector InertiaInv = (Mass > 0.0f) ? Inertia.Reciprocal() : FVector::ZeroVector;
					Dynamic->SetM(Mass);
					Dynamic->SetInvM(MassInv);
					Dynamic->SetI({ Inertia.X, Inertia.Y, Inertia.Z });
					Dynamic->SetInvI({ InertiaInv.X, InertiaInv.Y, InertiaInv.Z });
					Dynamic->SetLinearEtherDrag(BodyInstance->LinearDamping);
					Dynamic->SetAngularEtherDrag(BodyInstance->AngularDamping);
					Dynamic->Disabled() = true;
				}
			}
		}
	}

	FActorHandle::~FActorHandle()
	{
		if (ParticleHandle != nullptr)
		{
			Particles.DestroyParticle(ParticleHandle);
			ParticleHandle = nullptr;
			Geometry = nullptr;
		}
	}

	Chaos::TGenericParticleHandle<FReal, Dimensions> FActorHandle::Handle() const
	{
		return { ParticleHandle };
	}

	Chaos::TGeometryParticleHandle<FReal, Dimensions>* FActorHandle::GetParticle()
	{
		return ParticleHandle;
	}

	const Chaos::TGeometryParticleHandle<FReal, Dimensions>* FActorHandle::GetParticle() const
	{
		return ParticleHandle;
	}

	void FActorHandle::SetEnabled(bool bEnabled)
	{
		auto* Dynamic = ParticleHandle->CastToRigidParticle();
		if(Dynamic && Dynamic->ObjectState() == Chaos::EObjectStateType::Dynamic)
		{
			Dynamic->Disabled() = !bEnabled;
		}
	}

	void FActorHandle::SetWorldTransform(const FTransform& WorldTM)
	{
		using namespace Chaos;

		FParticleUtilities::SetActorWorldTransform(TGenericParticleHandle<FReal, 3>(ParticleHandle), WorldTM);

		auto* Dynamic = ParticleHandle->CastToRigidParticle();
		if(Dynamic && Dynamic->ObjectState() == Chaos::EObjectStateType::Dynamic)
		{
			Dynamic->X() = Dynamic->P();
			Dynamic->R() = Dynamic->Q();
		}
	}

	void FActorHandle::SetIsKinematic(bool bKinematic)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
		// This needs to destroy and recreate the particle
#endif
	}

	bool FActorHandle::GetIsKinematic() const
	{
		return Handle()->IsKinematic();
	}

	const FKinematicTarget& FActorHandle::GetKinematicTarget() const
	{
		check(ParticleHandle->CastToKinematicParticle());
		return ParticleHandle->CastToKinematicParticle()->KinematicTarget();
	}

	FKinematicTarget& FActorHandle::GetKinematicTarget()
	{
		check(ParticleHandle->CastToKinematicParticle());
		return ParticleHandle->CastToKinematicParticle()->KinematicTarget();
	}

	void FActorHandle::SetKinematicTarget(const FTransform& WorldTM)
	{
		using namespace Chaos;

		if (ensure(GetIsKinematic()))
		{
			FTransform ParticleTransform = FParticleUtilities::ActorWorldToParticleWorld(TGenericParticleHandle<FReal, 3>(ParticleHandle), WorldTM);
			GetKinematicTarget().SetTargetMode(ParticleTransform);
		}

	}

	bool FActorHandle::HasKinematicTarget() const
	{
		if (GetIsKinematic())
		{
			return GetKinematicTarget().GetMode() == Chaos::EKinematicTargetMode::Position;
		}
		return false;
	}

	bool FActorHandle::IsSimulated() const
	{
		return ParticleHandle->CastToRigidParticle() != nullptr && ParticleHandle->ObjectState() == Chaos::EObjectStateType::Dynamic;
	}

	FTransform FActorHandle::GetWorldTransform() const
	{
		using namespace Chaos;

		return FParticleUtilities::GetActorWorldTransform(TGenericParticleHandle<FReal, 3>(ParticleHandle));
	}

	void FActorHandle::SetLinearVelocity(const FVector& NewLinearVelocity)
	{
		using namespace Chaos;

		if (TKinematicGeometryParticleHandle<FReal, Dimensions>* KinematicParticleHandle = ParticleHandle->CastToKinematicParticle())
		{
			KinematicParticleHandle->SetV(NewLinearVelocity);
		}
	}

	FVector FActorHandle::GetLinearVelocity() const
	{
		return Handle()->V();
	}

	void FActorHandle::SetAngularVelocity(const FVector& NewAngularVelocity)
	{
		using namespace Chaos;

		if (TKinematicGeometryParticleHandle<FReal, Dimensions>* KinematicParticleHandle = ParticleHandle->CastToKinematicParticle())
		{
			KinematicParticleHandle->SetW(NewAngularVelocity);
		}
	}

	FVector FActorHandle::GetAngularVelocity() const
	{
		return Handle()->W();
	}

	void FActorHandle::AddForce(const FVector& Force)
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, 3>* Rigid = Handle()->CastToRigidParticle())
		{
			Rigid->F() += Force;
		}
	}

	void FActorHandle::AddRadialForce(const FVector& Origin, float Strength, float Radius, ERadialImpulseFalloff Falloff, EForceType ForceType)
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, 3>* Rigid = Handle()->CastToRigidParticle())
		{
			const FRigidTransform3& PCOMTransform = FParticleUtilities::GetCoMWorldTransform(Rigid);
			FVec3 Delta = PCOMTransform.GetTranslation() - Origin;

			const float Mag = Delta.Size();
			if (Mag > Radius)
			{
				return;
			}
			Delta.Normalize();

			float ImpulseMag = Strength;
			if (Falloff == RIF_Linear)
			{
				ImpulseMag *= (1.0f - (Mag / Radius));
			}

			const FVec3 PImpulse = Delta * ImpulseMag;
			const FVec3 ApplyDelta = (ForceType == EForceType::AddAcceleration || ForceType == EForceType::AddVelocity) ? PImpulse : PImpulse * Rigid->InvM();

			if (ForceType == EForceType::AddImpulse || ForceType == EForceType::AddVelocity)
			{
				Rigid->V() += ApplyDelta;
			}
			else
			{
				Rigid->F() += ApplyDelta;
			}
		}
	}

	void FActorHandle::AddImpulseAtLocation(FVector Impulse, FVector Location)
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, 3>* Rigid = Handle()->CastToRigidParticle())
		{
			FVector CoM = FParticleUtilities::GetCoMWorldPosition(Rigid);
			Rigid->LinearImpulse() += Impulse;
			Rigid->AngularImpulse() += FVector::CrossProduct(Location - CoM, Impulse);
		}
	}

	void FActorHandle::SetLinearDamping(float NewLinearDamping)
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, 3>* Rigid = Handle()->CastToRigidParticle())
		{
			Rigid->LinearEtherDrag() = NewLinearDamping;
		}
	}

	float FActorHandle::GetLinearDamping() const
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, 3>* Rigid = Handle()->CastToRigidParticle())
		{
			return Rigid->LinearEtherDrag();
		}
		return 0.0f;
	}

	void FActorHandle::SetAngularDamping(float NewAngularDamping)
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, 3>* Rigid = Handle()->CastToRigidParticle())
		{
			Rigid->AngularEtherDrag() = NewAngularDamping;
		}
	}

	float FActorHandle::GetAngularDamping() const
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, 3>* Rigid = Handle()->CastToRigidParticle())
		{
			return Rigid->AngularEtherDrag();
		}
		return 0.0f;
	}

	void FActorHandle::SetMaxLinearVelocitySquared(float NewMaxLinearVelocitySquared)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	float FActorHandle::GetMaxLinearVelocitySquared() const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
		return FLT_MAX;
	}

	void FActorHandle::SetMaxAngularVelocitySquared(float NewMaxAngularVelocitySquared)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	float FActorHandle::GetMaxAngularVelocitySquared() const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
		return FLT_MAX;
	}

	void FActorHandle::SetInverseMass(float NewInverseMass)
	{
		using namespace Chaos;

		TPBDRigidParticleHandle<FReal, Dimensions>* Dynamic = ParticleHandle->CastToRigidParticle();
		if(Dynamic && Dynamic->ObjectState() == EObjectStateType::Dynamic)
		{
			float NewMass = (NewInverseMass > SMALL_NUMBER) ? 1.0f / NewInverseMass : 0.0f;
			Dynamic->SetM(NewMass);
			Dynamic->SetInvM(NewInverseMass);
		}
	}

	float FActorHandle::GetInverseMass() const
	{
		return Handle()->InvM();
	}

	float FActorHandle::GetMass() const
	{
		return Handle()->M();
	}

	void FActorHandle::SetInverseInertia(const FVector& NewInverseInertia)
	{
		using namespace Chaos;

		TPBDRigidParticleHandle<FReal, Dimensions>* Dynamic = ParticleHandle->CastToRigidParticle();
		if(Dynamic && Dynamic->ObjectState() == EObjectStateType::Dynamic)
		{
			FVector NewInertia = FVector::ZeroVector;
			if ((NewInverseInertia.X > SMALL_NUMBER) && (NewInverseInertia.Y > SMALL_NUMBER) && (NewInverseInertia.Z > SMALL_NUMBER))
			{
				NewInertia = { 1.0f / NewInverseInertia.X , 1.0f / NewInverseInertia.Y, 1.0f / NewInverseInertia.Z };
			}
			Dynamic->SetI({ NewInertia.X, NewInertia.Y, NewInertia.Z });
			Dynamic->SetInvI({ NewInverseInertia.X, NewInverseInertia.Y, NewInverseInertia.Z });
		}
	}

	FVector FActorHandle::GetInverseInertia() const
	{
		using namespace Chaos;

		const PMatrix<float, 3, 3>& InvI = Handle()->InvI();
		return { InvI.M[0][0], InvI.M[1][1], InvI.M[2][2] };
	}

	FVector FActorHandle::GetInertia() const
	{
		using namespace Chaos;

		const PMatrix<float, 3, 3>& I = Handle()->I();
		return { I.M[0][0], I.M[1][1], I.M[2][2] };
	}

	void FActorHandle::SetMaxDepenetrationVelocity(float NewMaxDepenetrationVelocity)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	float FActorHandle::GetMaxDepenetrationVelocity(float NewMaxDepenetrationVelocity) const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
		return FLT_MAX;
	}

	void FActorHandle::SetMaxContactImpulse(float NewMaxContactImpulse)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	float FActorHandle::GetMaxContactImpulse() const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
		return FLT_MAX;
	}

	FTransform FActorHandle::GetLocalCoMTransform() const
	{
		return FTransform(Handle()->RotationOfMass(), Handle()->CenterOfMass());
	}

	int32 FActorHandle::GetLevel() const
	{
		return Level;
	}

	void FActorHandle::SetLevel(int32 InLevel)
	{
		Level = InLevel;
	}

}
