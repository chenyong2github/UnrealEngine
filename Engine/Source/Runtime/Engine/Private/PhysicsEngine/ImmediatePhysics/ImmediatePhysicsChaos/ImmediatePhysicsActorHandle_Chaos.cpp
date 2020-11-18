// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/MassProperties.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Sphere.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/Utilities.h"

#include "Physics/Experimental/ChaosInterfaceUtils.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyUtils.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace ImmediatePhysics_Chaos
{
	//
	// Utils
	//

	bool CreateDefaultGeometry(const FVector& Scale, float& OutMass, Chaos::TVector<float, 3>& OutInertia, Chaos::TRigidTransform<float, 3>& OutCoMTransform, TUniquePtr<Chaos::FImplicitObject>& OutGeom, TArray<TUniquePtr<Chaos::FPerShapeData>>& OutShapes)
	{
		using namespace Chaos;

		const FReal Mass = 1.0f;
		const FReal Radius = 1.0f * Scale.GetMax();

		auto ImplicitSphere = MakeUnique<Chaos::TSphere<float, 3>>(FVec3(0), Radius);
		auto NewShape = Chaos::FPerShapeData::CreatePerShapeData(OutShapes.Num());
		NewShape->SetGeometry(MakeSerializable(ImplicitSphere));
		NewShape->UpdateShapeBounds(FTransform::Identity);
		NewShape->SetUserData(nullptr);
		NewShape->SetQueryEnabled(false);
		NewShape->SetSimEnabled(false);

		OutMass = Mass;
		OutInertia = TSphere<FReal, 3>::GetInertiaTensor(Mass, Radius).GetDiagonal();
		OutCoMTransform = FTransform::Identity;
		OutShapes.Emplace(MoveTemp(NewShape));
		OutGeom = MoveTemp(ImplicitSphere);

		return true;
	}

#if WITH_CHAOS
	TUniquePtr<Chaos::FImplicitObject> CloneGeometry(const Chaos::FImplicitObject* Geom, TArray<TUniquePtr<Chaos::FPerShapeData>>& OutShapes)
	{
		using namespace Chaos;

		EImplicitObjectType GeomType = GetInnerType(Geom->GetCollisionType());
		bool bIsInstanced = IsInstanced(Geom->GetCollisionType());
		bool bIsScaled = IsScaled(Geom->GetCollisionType());

		// Transformed HeightField
		if (GeomType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<FReal, 3>* SrcTransformed = Geom->template GetObject<const TImplicitObjectTransformed<FReal, 3>>();
			if ((SrcTransformed != nullptr) && (SrcTransformed->GetTransformedObject()->GetType() == ImplicitObjectType::HeightField))
			{
				FImplicitObject* InnerGeom = const_cast<FImplicitObject*>(SrcTransformed->GetTransformedObject());
				TUniquePtr<TImplicitObjectTransformed<FReal, 3, false>> Cloned = MakeUnique<Chaos::TImplicitObjectTransformed<FReal, 3, false>>(InnerGeom, SrcTransformed->GetTransform());
				return Cloned;
			}
		}

		// Instanced Trimesh
		if (bIsInstanced && (GeomType == ImplicitObjectType::TriangleMesh))
		{
			const TImplicitObjectInstanced<FTriangleMeshImplicitObject>* SrcInstanced = Geom->template GetObject<const TImplicitObjectInstanced<FTriangleMeshImplicitObject>>();
			if (SrcInstanced != nullptr)
			{
				const TImplicitObjectInstanced<FTriangleMeshImplicitObject>::ObjectType InnerGeom = SrcInstanced->Object();
				TUniquePtr<TImplicitObjectInstanced<FTriangleMeshImplicitObject>> Cloned = MakeUnique<TImplicitObjectInstanced<FTriangleMeshImplicitObject>>(InnerGeom);
				return Cloned;
			}
		}

		return nullptr;
	}
#endif

	// Intended for use with Tri Mesh and Heightfields when cloning world simulation objects into the immediate scene
	bool CloneGeometry(FBodyInstance* BodyInstance, EActorType ActorType, const FVector& Scale, float& OutMass, Chaos::TVector<float, 3>& OutInertia, Chaos::TRigidTransform<float, 3>& OutCoMTransform, TUniquePtr<Chaos::FImplicitObject>& OutGeom, TArray<TUniquePtr<Chaos::FPerShapeData>>& OutShapes)
	{
#if WITH_CHAOS
		// We should only get non-simulated objects through this path, but you never know...
		if ((BodyInstance != nullptr) && !BodyInstance->bSimulatePhysics && (BodyInstance->ActorHandle != nullptr))
		{
			OutMass = 0.0f;
			OutInertia = FVector::ZeroVector;
			OutCoMTransform = FTransform::Identity;
			OutGeom = CloneGeometry(BodyInstance->ActorHandle->Geometry().Get(), OutShapes);
			if (OutGeom != nullptr)
			{
				return true;
			}
		}
#endif

		return CreateDefaultGeometry(Scale, OutMass, OutInertia, OutCoMTransform, OutGeom, OutShapes);
	}

	bool CreateGeometry(FBodyInstance* BodyInstance, EActorType ActorType, const FVector& Scale, float& OutMass, Chaos::TVector<float, 3>& OutInertia, Chaos::TRigidTransform<float, 3>& OutCoMTransform, TUniquePtr<Chaos::FImplicitObject>& OutGeom, TArray<TUniquePtr<Chaos::FPerShapeData>>& OutShapes)
	{
		using namespace Chaos;

		OutMass = 0.0f;
		OutInertia = FVector::ZeroVector;
		OutCoMTransform = FTransform::Identity;

		// If there's no BodySetup, we may be cloning an in-world object and probably have a TriMesh or HeightField so try to just copy references
		// @todo(ccaulfield): make this cleaner - we should have a separate path for this
		if ((BodyInstance == nullptr) || (BodyInstance->BodySetup == nullptr) || (BodyInstance->BodySetup->CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple))
		{
			return CloneGeometry(BodyInstance, ActorType, Scale, OutMass, OutInertia, OutCoMTransform, OutGeom, OutShapes);
		}

		UBodySetup* BodySetup = BodyInstance->GetBodySetup();

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
#if PHYSICS_INTERFACE_PHYSX
		AddParams.TriMeshes = TArrayView<PxTriangleMesh*>(BodySetup->TriMeshes);
#endif
#if WITH_CHAOS
		AddParams.ChaosTriMeshes = MakeArrayView(BodySetup->ChaosTriMeshes);
#endif

		TArray<TUniquePtr<FImplicitObject>> Geoms;
		FShapesArray Shapes;
		ChaosInterface::CreateGeometry(AddParams, Geoms, Shapes);

		if (Geoms.Num() == 0)
		{
			return false;
		}

#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
		if (ActorType == EActorType::DynamicActor)
		{
			// Whether each shape contributes to mass
			// @todo(chaos): it would be easier if ComputeMassProperties knew how to extract this info. Maybe it should be a flag in PerShapeData
			TArray<bool> bContributesToMass;
			bContributesToMass.Reserve(Shapes.Num());
			for (int32 ShapeIndex = 0; ShapeIndex < Shapes.Num(); ++ShapeIndex)
			{
				const TUniquePtr<FPerShapeData>& Shape = Shapes[ShapeIndex];
				const FKShapeElem* ShapeElem = FChaosUserData::Get<FKShapeElem>(Shape->GetUserData());
				bool bHasMass = ShapeElem && ShapeElem->GetContributeToMass();
				bContributesToMass.Add(bHasMass);
			}

			// bInertaScaleIncludeMass = true is to match legacy physics behaviour. This will scale the inertia by the change in mass (density x volumescale) 
			// as well as the dimension change even though we don't actually change the mass.
			const bool bInertaScaleIncludeMass = true;
			TMassProperties<float, 3> MassProperties = BodyUtils::ComputeMassProperties(BodyInstance, Shapes, bContributesToMass, FTransform::Identity, bInertaScaleIncludeMass);
			OutMass = MassProperties.Mass;
			OutInertia = MassProperties.InertiaTensor.GetDiagonal();
			OutCoMTransform = FTransform(MassProperties.RotationOfMass, MassProperties.CenterOfMass);
		}
#else
		OutMass = BodyInstance->GetBodyMass();
		OutInertia = BodyInstance->GetBodyInertiaTensor();
		OutCoMTransform = BodyInstance->GetMassSpaceLocal();
#endif

		// If we have multiple root shapes, wrap them in a union
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

	FActorHandle::FActorHandle(
		Chaos::TPBDRigidsSOAs<FReal, 3>& InParticles, 
		Chaos::TArrayCollectionArray<Chaos::FVec3>& InParticlePrevXs, 
		Chaos::TArrayCollectionArray<Chaos::FRotation3>& InParticlePrevRs, 
		EActorType ActorType, 
		FBodyInstance* BodyInstance, 
		const FTransform& InTransform)
		: Particles(InParticles)
		, ParticleHandle(nullptr)
		, ParticlePrevXs(InParticlePrevXs)
		, ParticlePrevRs(InParticlePrevRs)
	{
		using namespace Chaos;

		const FTransform Transform = FTransform(InTransform.GetRotation(), InTransform.GetTranslation());
		const FVector Scale = InTransform.GetScale3D();

		float Mass = 0;
		FVec3 Inertia = FVec3::OneVector;
		FRigidTransform3 CoMTransform = FRigidTransform3::Identity;
		if (CreateGeometry(BodyInstance, ActorType, Scale, Mass, Inertia, CoMTransform, Geometry, Shapes))
		{
			switch (ActorType)
			{
			case EActorType::StaticActor:
				ParticleHandle = Particles.CreateStaticParticles(1, nullptr, TGeometryParticleParameters<FReal, Dimensions>())[0];
				break;
			case EActorType::KinematicActor:
				ParticleHandle = Particles.CreateKinematicParticles(1, nullptr, TKinematicGeometryParticleParameters<FReal, Dimensions>())[0];
				break;
			case EActorType::DynamicActor:
				ParticleHandle = Particles.CreateDynamicParticles(1, nullptr, TPBDRigidParticleParameters<FReal, Dimensions>())[0];
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
				}

				auto* Dynamic = ParticleHandle->CastToRigidParticle();
				if (Dynamic && Dynamic->ObjectState() == EObjectStateType::Dynamic)
				{
					float MassInv = (Mass > 0.0f) ? 1.0f / Mass : 0.0f;
					FVector InertiaInv = (Mass > 0.0f) ? Inertia.Reciprocal() : FVector::ZeroVector;
					Dynamic->SetM(Mass);
					Dynamic->SetInvM(MassInv);
					Dynamic->SetCenterOfMass(CoMTransform.GetTranslation());
					Dynamic->SetRotationOfMass(CoMTransform.GetRotation());
					Dynamic->SetI({ Inertia.X, Inertia.Y, Inertia.Z });
					Dynamic->SetInvI({ InertiaInv.X, InertiaInv.Y, InertiaInv.Z });
					if (BodyInstance != nullptr)
					{
						Dynamic->SetLinearEtherDrag(BodyInstance->LinearDamping);
						Dynamic->SetAngularEtherDrag(BodyInstance->AngularDamping);
						Dynamic->SetGravityEnabled(BodyInstance->bEnableGravity);
					}
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


	void FActorHandle::InitWorldTransform(const FTransform& WorldTM)
	{
		using namespace Chaos;

		SetWorldTransform(WorldTM);

		if (auto* Kinematic = ParticleHandle->CastToKinematicParticle())
		{
			Kinematic->V() = FVec3(0);
			Kinematic->W() = FVec3(0);
			Kinematic->KinematicTarget().Clear();
		}
	}

	void FActorHandle::SetWorldTransform(const FTransform& WorldTM)
	{
		using namespace Chaos;

		FParticleUtilitiesXR::SetActorWorldTransform(TGenericParticleHandle<FReal, 3>(ParticleHandle), WorldTM);

		auto* Dynamic = ParticleHandle->CastToRigidParticle();
		if(Dynamic && Dynamic->ObjectState() == Chaos::EObjectStateType::Dynamic)
		{
			Dynamic->P() = Dynamic->X();
			Dynamic->Q() = Dynamic->R();
			Dynamic->AuxilaryValue(ParticlePrevXs) = Dynamic->P();
			Dynamic->AuxilaryValue(ParticlePrevRs) = Dynamic->Q();
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
			TGenericParticleHandle<FReal, 3> GenericHandle(ParticleHandle);
			FTransform PreviousTransform(GenericHandle->R(), GenericHandle->X());
			FTransform ParticleTransform = FParticleUtilities::ActorWorldToParticleWorld(GenericHandle, WorldTM);

			GetKinematicTarget().SetTargetMode(ParticleTransform, PreviousTransform);
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
