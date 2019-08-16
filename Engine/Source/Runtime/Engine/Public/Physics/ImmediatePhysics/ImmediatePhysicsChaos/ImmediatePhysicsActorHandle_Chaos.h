// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsCore_Chaos.h"

#include "Engine/EngineTypes.h"

namespace ImmediatePhysics_Chaos
{

	/** handle associated with a physics actor. This is the proper way to read/write to the physics simulation */
	struct ENGINE_API FActorHandle
	{
	public:
		~FActorHandle();

		void SetEnabled(bool bEnabled);

		/** Sets the world transform.*/
		void SetWorldTransform(const FTransform& WorldTM);

		/** Make a body kinematic, or non-kinematic */
		void SetIsKinematic(bool bKinematic);

		/** Is the actor kinematic */
		bool GetIsKinematic() const;

#if IMMEDIATEPHYSICS_CHAOS_TODO
		/** Gets the kinematic target (next transform) for the actor if one is set (check HasKinematicTarget() to see if a target is available) */
		FImmediateKinematicTarget& GetKinematicTarget();
#endif

		/** Sets the kinematic target. This will affect velocities as expected*/
		void SetKinematicTarget(const FTransform& WorldTM);

		/** Does this actor have a kinematic target (next kinematic transform to be applied) */
		bool HasKinematicTarget() const;

		/** Whether the body is simulating */
		bool IsSimulated() const;

		/** Get the world transform */
		FTransform GetWorldTransform() const;

		/** Set the linear velocity */
		void SetLinearVelocity(const FVector& NewLinearVelocity);

		/** Get the linear velocity */
		FVector GetLinearVelocity() const;

		/** Set the angular velocity */
		void SetAngularVelocity(const FVector& NewAngularVelocity);

		/** Get the angular velocity */
		FVector GetAngularVelocity() const;

		void AddForce(const FVector& Force);

		void AddRadialForce(const FVector& Origin, float Strength, float Radius, ERadialImpulseFalloff Falloff, EForceType ForceType);

		/** Set the linear damping*/
		void SetLinearDamping(float NewLinearDamping);

		/** Get the linear damping*/
		float GetLinearDamping() const;

		/** Set the angular damping*/
		void SetAngularDamping(float NewAngularDamping);

		/** Get the angular damping*/
		float GetAngularDamping() const;

		/** Set the max linear velocity squared*/
		void SetMaxLinearVelocitySquared(float NewMaxLinearVelocitySquared);

		/** Get the max linear velocity squared*/
		float GetMaxLinearVelocitySquared() const;

		/** Set the max angular velocity squared*/
		void SetMaxAngularVelocitySquared(float NewMaxAngularVelocitySquared);

		/** Get the max angular velocity squared*/
		float GetMaxAngularVelocitySquared() const;

		/** Set the inverse mass. 0 indicates kinematic object */
		void SetInverseMass(float NewInverseMass);

		/** Get the inverse mass. */
		float GetInverseMass() const;

		/** Set the inverse inertia. Mass-space inverse inertia diagonal vector */
		void SetInverseInertia(const FVector& NewInverseInertia);

		/** Get the inverse inertia. Mass-space inverse inertia diagonal vector */
		FVector GetInverseInertia() const;

		/** Set the max depenetration velocity*/
		void SetMaxDepenetrationVelocity(float NewMaxDepenetrationVelocity);

		/** Get the max depenetration velocity*/
		float GetMaxDepenetrationVelocity(float NewMaxDepenetrationVelocity) const;

		/** Set the max contact impulse*/
		void SetMaxContactImpulse(float NewMaxContactImpulse);

		/** Get the max contact impulse*/
		float GetMaxContactImpulse() const;

	private:
		friend struct FSimulation;
		friend struct FJointHandle;

		FActorHandle(Chaos::TPBDRigidsEvolutionGBF<FReal, Dimensions>* InEvolution, EActorType ActorType, FBodyInstance* BodyInstance, const FTransform& Transform);

		Chaos::TGenericParticleHandle<FReal, Dimensions> Handle() const;

		Chaos::TPBDRigidsEvolutionGBF<FReal, Dimensions>* Evolution;
		Chaos::TGeometryParticleHandle<FReal, Dimensions>* ParticleHandle;
		TUniquePtr<Chaos::TImplicitObject<float, 3>> Geometry;
		TArray<TUniquePtr<Chaos::TPerShapeData<float, 3>>> Shapes;
		FVector ActorToCoMTranslation;
	};

}

#endif // INCLUDE_CHAOS