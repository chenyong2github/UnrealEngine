// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsCore_Chaos.h"

#include "Chaos/ChaosDebugDrawDeclares.h"
#include "Chaos/Evolution/PBDMinEvolution.h"
#include "Chaos/Collision/CollisionDetector.h"
#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/PBDJointConstraints.h"

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
		void Simulate(float DeltaTime, float MaxDeltaTime, int32 MaxSubSteps, const FVector& InGravity);
		void Simulate_AssumesLocked(float DeltaTime, float MaxDeltaTime, int32 MaxSubSteps, const FVector& InGravity) { Simulate(DeltaTime, MaxDeltaTime, MaxSubSteps, InGravity); }

		///** Whether or not an entity is simulated */
		//bool IsSimulated(int32 ActorDataIndex) const;


		///** Add a radial impulse to the given actor */
		//void AddRadialForce(int32 ActorDataIndex, const FVector& Origin, float Strength, float Radius, ERadialImpulseFalloff Falloff, EForceType ForceType);

		///** Add a force to the given actor */
		//void AddForce(int32 ActorDataIndex, const FVector& Force);

		///* Set solver iteration counts per step */
		//void SetPositionIterationCount(int32 InIterationCount);
		//void SetVelocityIterationCount(int32 InIterationCount);

		void SetSimulationSpaceTransform(const FTransform& Transform) { SimulationSpaceTransform = Transform; }

	private:
		void ConditionConstraints();

		void DebugDrawKinematicParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color);
		void DebugDrawDynamicParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color);
		void DebugDrawConstraints(const int32 MinDebugLevel, const int32 MaxDebugLevel, const float ColorScale);

		using FCollisionConstraints = Chaos::TPBDCollisionConstraints<FReal, 3>;
		using FCollisionDetector = Chaos::TCollisionDetector<Chaos::FParticlePairBroadPhase, Chaos::FNarrowPhase, Chaos::FSyncCollisionReceiver, FCollisionConstraints>;
		using FRigidParticleSOAs = Chaos::TPBDRigidsSOAs<FReal, 3>;
		using FParticleHandle = Chaos::TGeometryParticleHandle<FReal, Dimensions>;
		using FParticlePair = Chaos::TVector<Chaos::TGeometryParticleHandle<Chaos::FReal, 3>*, 2>;

		// @todo(ccaulfield): Look into these...
		TArray<FParticlePair> PotentiallyCollidingPairs;
		Chaos::TArrayCollectionArray<bool> CollidedParticles;
		Chaos::TArrayCollectionArray<Chaos::TSerializablePtr<Chaos::FChaosPhysicsMaterial>> ParticleMaterials;
		Chaos::TArrayCollectionArray<TUniquePtr<Chaos::FChaosPhysicsMaterial>> PerParticleMaterials;

		FRigidParticleSOAs Particles;
		Chaos::FPBDJointConstraints Joints;
		FCollisionConstraints Collisions;
		Chaos::FParticlePairBroadPhase BroadPhase;
		FCollisionDetector CollisionDetector;
		Chaos::TSimpleConstraintRule<Chaos::FPBDJointConstraints> JointsRule;
		Chaos::TSimpleConstraintRule<FCollisionConstraints> CollisionsRule;
		Chaos::FPBDMinEvolution Evolution;


		/** Mapping from entity index to handle */
		// @todo(ccaulfield): we now have handles pointing to handles which is inefficient - we can do better than this, but don't want to change API yet
		TArray<FActorHandle*> ActorHandles;
		int32 NumActiveDynamicActorHandles;

		/** Mapping from constraint index to handle */
		TArray<FJointHandle*> JointHandles;

		/** Slow to access. */
		// @todo(ccaulfield): Optimize
		TMap<const FParticleHandle*, TSet<const FParticleHandle*>> IgnoreCollisionParticlePairTable;

		FTransform SimulationSpaceTransform;
	};

}
