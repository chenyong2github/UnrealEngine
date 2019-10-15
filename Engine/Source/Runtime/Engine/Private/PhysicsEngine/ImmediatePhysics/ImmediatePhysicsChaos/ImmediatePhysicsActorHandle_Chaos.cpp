// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/MassProperties.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Sphere.h"

#include "Physics/Experimental/ChaosInterfaceUtils.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"

extern int32 ImmediatePhysicsDisableCollisions;

namespace ImmediatePhysics_Chaos
{
	//
	// Utils
	//

	template<typename T, int d>
	Chaos::PMatrix<T, d, d> CalculateInertia_Solid(const T Mass, const FKSphereElem& SphereElem)
	{
		return Chaos::PMatrix<T, d, d>(
			((T)2 / (T)5) * Mass * SphereElem.Radius * SphereElem.Radius,
			((T)2 / (T)5) * Mass * SphereElem.Radius * SphereElem.Radius,
			((T)2 / (T)5) * Mass * SphereElem.Radius * SphereElem.Radius
			);
	}

	template<typename T, int d>
	Chaos::PMatrix<T, d, d> CalculateInertia_Solid(const T Mass, const FKSphylElem& SphylElem)
	{
		return Chaos::PMatrix<T, d, d>(
			((T)1 / (T)12) * Mass * ((T)3 * SphylElem.Radius + SphylElem.Length),
			((T)1 / (T)12) * Mass * ((T)3 * SphylElem.Radius + SphylElem.Length),
			((T)1 / (T)2) * Mass * SphylElem.Radius * SphylElem.Radius
			);
	}

	template<typename T, int d>
	Chaos::PMatrix<T, d, d> CalculateInertia_Solid(const T Mass, const FKBoxElem& BoxElem)
	{
		return Chaos::PMatrix<T, d, d>(
			((T)1 / (T)12) * Mass * (BoxElem.Y * BoxElem.Y + BoxElem.Z * BoxElem.Z),
			((T)1 / (T)12) * Mass * (BoxElem.Z * BoxElem.Z + BoxElem.X * BoxElem.X),
			((T)1 / (T)12) * Mass * (BoxElem.X * BoxElem.X + BoxElem.Y * BoxElem.Y)
			);
	}

	template<typename T, int d>
	void CalculateMassProperties(const FVector& Scale, const FTransform& LocalTransform, const FKAggregateGeom& AggGeom, Chaos::TMassProperties<T, d>& OutMassProperties)
	{
		using namespace Chaos;
		TArray<TMassProperties<T, d>> AllMassProperties;

		for (uint32 i = 0; i < static_cast<uint32>(AggGeom.SphereElems.Num()); ++i)
		{
			const FKSphereElem ScaledSphereElem = AggGeom.SphereElems[i].GetFinalScaled(Scale, LocalTransform);

			TMassProperties<T, d> MassProperties;
			MassProperties.CenterOfMass = LocalTransform.GetTranslation() + ScaledSphereElem.Center;
			MassProperties.RotationOfMass = TRotation<T, d>::FromIdentity();
			MassProperties.Volume = ScaledSphereElem.GetVolume(FVector::OneVector);
			MassProperties.InertiaTensor = CalculateInertia_Solid<T, d>(MassProperties.Volume, ScaledSphereElem);

			AllMassProperties.Add(MassProperties);
		}
		for (uint32 i = 0; i < static_cast<uint32>(AggGeom.BoxElems.Num()); ++i)
		{
			const auto& BoxElem = AggGeom.BoxElems[i];

			TMassProperties<T, d> MassProperties;
			MassProperties.CenterOfMass = LocalTransform.GetTranslation() + BoxElem.Center;
			MassProperties.RotationOfMass = LocalTransform.GetRotation() * TRotation<T, d>(FQuat(BoxElem.Rotation));
			MassProperties.Volume = BoxElem.GetVolume(Scale);
			MassProperties.InertiaTensor = CalculateInertia_Solid<T, d>(MassProperties.Volume, BoxElem);

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

				TMassProperties<T, d> MassProperties;
				MassProperties.CenterOfMass = LocalTransform.GetTranslation() + ScaledSphereElem.Center;
				MassProperties.RotationOfMass = TRotation<T, d>::FromIdentity();
				MassProperties.Volume = ScaledSphereElem.GetVolume(FVector::OneVector);
				MassProperties.InertiaTensor = CalculateInertia_Solid<T, d>(MassProperties.Volume, ScaledSphereElem);

				AllMassProperties.Add(MassProperties);
			}
			else
			{
				TMassProperties<T, d> MassProperties;
				MassProperties.CenterOfMass = LocalTransform.GetTranslation() + ScaledSphylElem.Center;
				MassProperties.RotationOfMass = LocalTransform.GetRotation() * TRotation<T, d>(FQuat(ScaledSphylElem.Rotation));
				MassProperties.Volume = ScaledSphylElem.GetVolume(FVector::OneVector);
				MassProperties.InertiaTensor = CalculateInertia_Solid<T, d>(MassProperties.Volume, ScaledSphylElem);

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

		OutMassProperties = Combine(AllMassProperties);
	}


	bool CreateGeometry(FBodyInstance* BodyInstance, const FVector& Scale, float& OutMass, Chaos::TVector<float, 3>& OutInertia, Chaos::TRigidTransform<float, 3>& OutCoMTransform, TUniquePtr<Chaos::TImplicitObject<FReal, Dimensions>>& OutGeom, TArray<TUniquePtr<Chaos::TPerShapeData<float, 3>>>& OutShapes)
	{
		UBodySetup* BodySetup = BodyInstance->BodySetup.Get();

#if WITH_CHAOS && !PHYSICS_INTERFACE_PHYSX
		float Density = 1.e-3f;	// 1g/cm3
		Chaos::TMassProperties<float, 3> MassProperties;
		CalculateMassProperties<float, 3>(Scale, FTransform::Identity, BodySetup->AggGeom, MassProperties);
		OutMass = Density * MassProperties.Volume;
		OutInertia = Density * Chaos::TVector<float, 3>(MassProperties.InertiaTensor.M[0][0], MassProperties.InertiaTensor.M[1][1], MassProperties.InertiaTensor.M[2][2]);
		OutCoMTransform = FTransform(MassProperties.RotationOfMass, MassProperties.CenterOfMass);
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
		AddParams.LocalTransform = Chaos::TRigidTransform<float, 3>(OutCoMTransform.GetRotation().Inverse() * -OutCoMTransform.GetTranslation(), OutCoMTransform.GetRotation().Inverse());
		AddParams.Geometry = &BodySetup->AggGeom;
#if WITH_PHYSX
		AddParams.TriMeshes = TArrayView<PxTriangleMesh*>(BodySetup->TriMeshes);
#endif
#if WITH_CHAOS
		AddParams.ChaosTriMeshes = MakeArrayView(BodySetup->ChaosTriMeshes);
#endif

		TArray<TUniquePtr<Chaos::TImplicitObject<float, 3>>> Geoms;
		TArray<TUniquePtr<Chaos::TPerShapeData<float, 3>>, TInlineAllocator<1>> Shapes;
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
			OutGeom = MakeUnique<Chaos::TImplicitObjectUnion<float, 3>>(MoveTemp(Geoms));
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

	FActorHandle::FActorHandle(Chaos::TPBDRigidsEvolutionGBF<FReal, Dimensions>* InEvolution, EActorType ActorType, FBodyInstance* BodyInstance, const FTransform& Transform)
		: Evolution(InEvolution)
		, ParticleHandle(nullptr)
	{
		using namespace Chaos;

		// @todo(ccaulfield): Scale
		float Mass = 0;
		TVector<float, 3> Inertia = TVector<float, 3>::OneVector;
		TRigidTransform<float, 3> CoMTransform = TRigidTransform<float, 3>::Identity;
		if (CreateGeometry(BodyInstance, FVector::OneVector, Mass, Inertia, CoMTransform, Geometry, Shapes))
		{
			ActorToCoMTransform = CoMTransform;

			switch (ActorType)
			{
			case EActorType::StaticActor:
				ParticleHandle = Evolution->CreateStaticParticles(1, TGeometryParticleParameters<FReal, Dimensions>())[0];
				break;
			case EActorType::KinematicActor:
				ParticleHandle = Evolution->CreateKinematicParticles(1, TKinematicGeometryParticleParameters<FReal, Dimensions>())[0];
				break;
			case EActorType::DynamicActor:
				ParticleHandle = Evolution->CreateDynamicParticles(1, TPBDRigidParticleParameters<FReal, Dimensions>())[0];
				break;
			}

			if (ParticleHandle != nullptr)
			{
				SetWorldTransform(Transform);

				ParticleHandle->SetGeometry(MakeSerializable(Geometry));

				if (auto* Kinematic = ParticleHandle->AsKinematic())
				{
					Kinematic->SetV(FVector::ZeroVector);
					Kinematic->SetW(FVector::ZeroVector);
				}

				if (auto* Dynamic = ParticleHandle->AsDynamic())
				{
					float MassInv = (Mass > 0.0f) ? 1.0f / Mass : 0.0f;
					FVector InertiaInv = (Mass > 0.0f) ? Inertia.Reciprocal() : FVector::ZeroVector;
					Dynamic->SetM(Mass);
					Dynamic->SetInvM(MassInv);
					Dynamic->SetI({ Inertia.X, Inertia.Y, Inertia.Z });
					Dynamic->SetInvI({ InertiaInv.X, InertiaInv.Y, InertiaInv.Z });
					Dynamic->Disabled() = true;
				}
			}
		}
	}

	FActorHandle::~FActorHandle()
	{
		if (ParticleHandle != nullptr)
		{
			Evolution->DestroyParticle(ParticleHandle);
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
		if (auto* Dynamic = ParticleHandle->AsDynamic())
		{
			Dynamic->Disabled() = !bEnabled;
		}
	}

	void FActorHandle::SetWorldTransform(const FTransform& WorldTM)
	{
		FTransform ParticleTransform = ActorToCoMTransform * WorldTM;

		ParticleHandle->SetX(ParticleTransform.GetTranslation());
		ParticleHandle->SetR(ParticleTransform.GetRotation());

		if (auto* Dynamic = ParticleHandle->AsDynamic())
		{
			Dynamic->SetP(Dynamic->X());
			Dynamic->SetQ(Dynamic->R());
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
		check(ParticleHandle->AsKinematic());
		return ParticleHandle->AsKinematic()->KinematicTarget();
	}

	FKinematicTarget& FActorHandle::GetKinematicTarget()
	{
		check(ParticleHandle->AsKinematic());
		return ParticleHandle->AsKinematic()->KinematicTarget();
	}

	void FActorHandle::SetKinematicTarget(const FTransform& WorldTM)
	{
		if (ensure(GetIsKinematic()))
		{
			FTransform ParticleTransform = ActorToCoMTransform * WorldTM;
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
		return ParticleHandle->AsDynamic() != nullptr;
	}

	FTransform FActorHandle::GetWorldTransform() const
	{
		FTransform ParticleTransform = FTransform(Handle()->R(), Handle()->X());
		return ActorToCoMTransform.GetRelativeTransformReverse(ParticleTransform);
	}

	void FActorHandle::SetLinearVelocity(const FVector& NewLinearVelocity)
	{
		using namespace Chaos;

		if (TKinematicGeometryParticleHandle<FReal, Dimensions>* KinematicParticleHandle = ParticleHandle->AsKinematic())
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

		if (TKinematicGeometryParticleHandle<FReal, Dimensions>* KinematicParticleHandle = ParticleHandle->AsKinematic())
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
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	void FActorHandle::AddRadialForce(const FVector& Origin, float Strength, float Radius, ERadialImpulseFalloff Falloff, EForceType ForceType)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	void FActorHandle::SetLinearDamping(float NewLinearDamping)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	float FActorHandle::GetLinearDamping() const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
		return 0.0f;
	}

	void FActorHandle::SetAngularDamping(float NewAngularDamping)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
	}

	float FActorHandle::GetAngularDamping() const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
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

		if (TPBDRigidParticleHandle<FReal, Dimensions>* Dynamic = ParticleHandle->AsDynamic())
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

	void FActorHandle::SetInverseInertia(const FVector& NewInverseInertia)
	{
		using namespace Chaos;

		if (TPBDRigidParticleHandle<FReal, Dimensions>* Dynamic = ParticleHandle->AsDynamic())
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

		PMatrix<float, 3, 3> InvI = Handle()->InvI();
		return { InvI.M[0][0], InvI.M[1][1], InvI.M[2][2] };
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

	const FTransform& FActorHandle::GetLocalCoMTransform() const
	{
		return ActorToCoMTransform;
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
