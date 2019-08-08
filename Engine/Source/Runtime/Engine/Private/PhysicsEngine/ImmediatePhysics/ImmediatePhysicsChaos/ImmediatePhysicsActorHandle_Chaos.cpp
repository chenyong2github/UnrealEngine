// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"

#if INCLUDE_CHAOS

#include "Chaos/Box.h"
#include "Chaos/Capsule.h"
#include "Chaos/GeometryParticles.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/Sphere.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"

namespace ImmediatePhysics_Chaos
{
	//
	// Utils
	//

	bool CreateGeometry(FBodyInstance* BodyInstance, const FVector& Scale, TUniquePtr<Chaos::TImplicitObject<FReal, Dimensions>>& OutGeom, TArray<TUniquePtr<Chaos::TPerShapeData<float, 3>>>& OutShapes)
	{
		// @todo(ccaulfield): copied from FPhysInterface_Chaos::AddGeometry. Merge implementations.
		TArray<TUniquePtr<Chaos::TImplicitObject<float, 3>>> Geoms;
		TArray<TUniquePtr<Chaos::TPerShapeData<float, 3>>> Shapes;

		FBodyCollisionData CollisionData;
		BodyInstance->BuildBodyFilterData(CollisionData.CollisionFilterData);
		BodyInstance->BuildBodyCollisionFlags(CollisionData.CollisionFlags, BodyInstance->GetCollisionEnabled(), BodyInstance->BodySetup->GetCollisionTraceFlag() == CTF_UseComplexAsSimple);

		FKAggregateGeom* Geometry = &BodyInstance->BodySetup->AggGeom;
		FVector ActorToCoMTranslation = BodyInstance->GetMassSpaceLocal().GetTranslation();

		auto NewShapeHelper = [CollisionData](const Chaos::TImplicitObject<float, 3>* InGeom, bool bComplexShape = false)
		{
			auto NewShape = MakeUnique<Chaos::TPerShapeData<float, 3>>();
			NewShape->Geometry = InGeom;
			NewShape->QueryData = bComplexShape ? CollisionData.CollisionFilterData.QueryComplexFilter : CollisionData.CollisionFilterData.QuerySimpleFilter;
			NewShape->SimData = CollisionData.CollisionFilterData.SimFilter;
			return NewShape;
		};

		for (uint32 i = 0; i < static_cast<uint32>(Geometry->SphereElems.Num()); ++i)
		{
			ensure(FMath::IsNearlyEqual(Scale[0], Scale[1]) && FMath::IsNearlyEqual(Scale[1], Scale[2]));
			const auto& CollisionSphere = Geometry->SphereElems[i];
			auto ImplicitSphere = MakeUnique<Chaos::TSphere<float, 3>>(Chaos::TVector<float, 3>(0.f, 0.f, 0.f), CollisionSphere.Radius * Scale[0]);
			auto NewShape = NewShapeHelper(ImplicitSphere.Get());
			Shapes.Emplace(MoveTemp(NewShape));
			Geoms.Add(MoveTemp(ImplicitSphere));

		}
		for (uint32 i = 0; i < static_cast<uint32>(Geometry->BoxElems.Num()); ++i)
		{
			const auto& Box = Geometry->BoxElems[i];
			Chaos::TVector<float, 3> half_extents = Scale * Chaos::TVector<float, 3>(Box.X / 2.f, Box.Y / 2.f, Box.Z / 2.f);
			auto ImplicitBox = MakeUnique<Chaos::TBox<float, 3>>(-half_extents, half_extents);
			auto NewShape = NewShapeHelper(ImplicitBox.Get());
			Shapes.Emplace(MoveTemp(NewShape));
			Geoms.Add(MoveTemp(ImplicitBox));

		}
		for (uint32 i = 0; i < static_cast<uint32>(Geometry->SphylElems.Num()); ++i)
		{
			ensure(FMath::IsNearlyEqual(Scale[0], Scale[1]) && FMath::IsNearlyEqual(Scale[1], Scale[2]));
			const auto& Sphyl = Geometry->SphylElems[i];
			if (Sphyl.Length == 0)
			{
				auto ImplicitSphere = MakeUnique<Chaos::TSphere<float, 3>>(Chaos::TVector<float, 3>(0), Sphyl.Radius * Scale[0]);
				auto NewShape = NewShapeHelper(ImplicitSphere.Get());
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(ImplicitSphere));

			}
			else
			{
				Chaos::TVector<float, 3> HalfExtents(0, 0, Sphyl.Length / 2 * Scale[0]);
				FTransform SphylTransform = Sphyl.GetTransform();
				SphylTransform.SetTranslation(SphylTransform.GetTranslation() - ActorToCoMTranslation);

				auto ImplicitCapsule = MakeUnique<Chaos::TCapsule<float>>(SphylTransform.TransformPosition(-HalfExtents), SphylTransform.TransformPosition(HalfExtents), Sphyl.Radius * Scale[0]);
				auto NewShape = NewShapeHelper(ImplicitCapsule.Get());
				Shapes.Emplace(MoveTemp(NewShape));
				Geoms.Add(MoveTemp(ImplicitCapsule));
			}
		}

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

		OutShapes = MoveTemp(Shapes);

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
		if (CreateGeometry(BodyInstance, FVector::OneVector, Geometry, Shapes))
		{
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
				ActorToCoMTranslation = BodyInstance->GetMassSpaceLocal().GetTranslation();

				SetWorldTransform(Transform);

				ParticleHandle->SetGeometry(MakeSerializable(Geometry));

				if (auto* Kinematic = ParticleHandle->AsKinematic())
				{
					Kinematic->SetV(FVector::ZeroVector);
					Kinematic->SetW(FVector::ZeroVector);
				}

				if (auto* Dynamic = ParticleHandle->AsDynamic())
				{
					float Mass = BodyInstance->GetBodyMass();
					float MassInv = (Mass > 0.0f) ? 1.0f / Mass : 0.0f;
					FVector Inertia = BodyInstance->GetBodyInertiaTensor();
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

	void FActorHandle::SetEnabled(bool bEnabled)
	{
		if (auto* Dynamic = ParticleHandle->AsDynamic())
		{
			Dynamic->Disabled() = !bEnabled;
		}
	}

	void FActorHandle::SetWorldTransform(const FTransform& WorldTM)
	{
		FTransform ParticleTransform = FTransform(WorldTM.GetRotation(), WorldTM.TransformPosition(ActorToCoMTranslation));

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
#endif
	}

	bool FActorHandle::GetIsKinematic() const
	{
		return Handle()->IsKinematic();
	}


#if IMMEDIATEPHYSICS_CHAOS_TODO
	FImmediateKinematicTarget& FActorHandle::GetKinematicTarget()
	{
	}
#endif

	void FActorHandle::SetKinematicTarget(const FTransform& WorldTM)
	{
		using namespace Chaos;

#if IMMEDIATEPHYSICS_CHAOS_TODO
		// Proper kinematic targets
#endif
		if (TKinematicGeometryParticleHandle<FReal, Dimensions>* KinematicParticleHandle = ParticleHandle->AsKinematic())
		{
			ParticleHandle->SetX(WorldTM.GetTranslation());
			ParticleHandle->SetR(WorldTM.GetRotation());
		}
	}

	bool FActorHandle::HasKinematicTarget() const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
		return false;
	}

	bool FActorHandle::IsSimulated() const
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
#endif
		return true;
	}

	FTransform FActorHandle::GetWorldTransform() const
	{
		FTransform ParticleTransform = FTransform(Handle()->R(), Handle()->X());
		return FTransform(ParticleTransform.GetRotation(), ParticleTransform.GetTranslation() - ParticleTransform.TransformVector(ActorToCoMTranslation));
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

}

#endif // INCLUDE_CHAOS
