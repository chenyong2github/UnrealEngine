// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsCore_Chaos.h"

#include "Engine/EngineTypes.h"
#include "Templates/UniquePtr.h"

namespace ImmediatePhysics_Chaos
{
	/** Owns all the data associated with the simulation. Can be considered a single scene or world */
	struct ENGINE_API FSimulation
	{
	public:
		FSimulation();
		~FSimulation();

		int32 NumActors() const { return ActorHandles.Num(); }

		FActorHandle* GetActorHandle(int32 ActorHandleIndex) { return ActorHandles[ActorHandleIndex]; }
		const FActorHandle* GetActorHandle(int32 ActorHandleIndex) const { return ActorHandles[ActorHandleIndex]; }

		/** Create a static body and add it to the simulation */
		FActorHandle* CreateStaticActor(FBodyInstance* BodyInstance);

		/** Create a kinematic body and add it to the simulation */
		FActorHandle* CreateKinematicActor(FBodyInstance* BodyInstance, const FTransform& Transform);

		/** Create a dynamic body and add it to the simulation */
		FActorHandle* CreateDynamicActor(FBodyInstance* BodyInstance, const FTransform& Transform);

		FActorHandle* CreateActor(EActorType ActorType, FBodyInstance* BodyInstance, const FTransform& Transform);
		void DestroyActor(FActorHandle* ActorHandle);

		/** Create a physical joint and add it to the simulation */
		FJointHandle* CreateJoint(FConstraintInstance* ConstraintInstance, FActorHandle* Body1, FActorHandle* Body2);
		void DestroyJoint(FJointHandle* JointHandle);

		/** Sets the number of active bodies. This number is reset any time a new simulated body is created */
		void SetNumActiveBodies(int32 NumActiveBodies);

		/** An array of actors to ignore. */
		struct FIgnorePair
		{
			FActorHandle* A;
			FActorHandle* B;
		};

		/** Set pair of bodies to ignore collision for */
		void SetIgnoreCollisionPairTable(const TArray<FIgnorePair>& InIgnoreCollisionPairTable);

		/** Set bodies that require no collision */
		void SetIgnoreCollisionActors(const TArray<FActorHandle*>& InIgnoreCollisionActors);

		///** Get/Set whether the body is kinematic or not, kinematics do not simulate and move where they're told. They also act as if they have infinite mass */
		//bool GetIsKinematic(int32 ActorDataIndex);
		//void SetIsKinematic(int32 ActorDataIndex, bool bKinematic);

		/** Advance the simulation by DeltaTime */
		void Simulate(float DeltaTime, const FVector& InGravity);
		void Simulate_AssumesLocked(float DeltaTime, const FVector& InGravity) { Simulate(DeltaTime, InGravity); }

		///** Whether or not an entity is simulated */
		//bool IsSimulated(int32 ActorDataIndex) const;


		///** Add a radial impulse to the given actor */
		//void AddRadialForce(int32 ActorDataIndex, const FVector& Origin, float Strength, float Radius, ERadialImpulseFalloff Falloff, EForceType ForceType);

		///** Add a force to the given actor */
		//void AddForce(int32 ActorDataIndex, const FVector& Force);

		///* Set solver iteration counts per step */
		//void SetPositionIterationCount(int32 InIterationCount);
		//void SetVelocityIterationCount(int32 InIterationCount);

	private:
		TUniquePtr<Chaos::TPBDRigidsEvolutionGBF<FReal, Dimensions>> Evolution;
		TUniquePtr<Chaos::TPBDRigidsSOAs<FReal, Dimensions>> Particles;
		TUniquePtr<Chaos::TPBDJointConstraints<FReal, Dimensions>> Joints;
		TUniquePtr<Chaos::TPBDConstraintIslandRule<Chaos::TPBDJointConstraints<FReal, Dimensions>, FReal, Dimensions>> JointsRule;		// @todo(ccaulfield): Don't need islands for anim node physics...

		/** Mapping from entity index to handle */
		// @todo(ccaulfield): we now have handles pointing to handles which is inefficient - we can do better than this, but don't want to change API yet
		TArray<FActorHandle*> ActorHandles;
		int NumActiveActorHandles;

		/** Mapping from constraint index to handle */
		TArray<FJointHandle*> JointHandles;

		/** Both of these are slow to access. Make sure to use iteration cache when possible */
		TMap<FActorHandle*, TSet<FActorHandle*>> IgnoreCollisionPairTable;
		TSet<FActorHandle*> IgnoreCollisionActors;

		FVector Gravity;
	};

}

#endif // INCLUDE_CHAOS