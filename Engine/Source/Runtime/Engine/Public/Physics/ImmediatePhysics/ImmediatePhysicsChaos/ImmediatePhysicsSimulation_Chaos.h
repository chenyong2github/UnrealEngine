// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

		int32 NumActors() const;

		FActorHandle* GetActorHandle(int32 ActorHandleIndex);
		const FActorHandle* GetActorHandle(int32 ActorHandleIndex) const;

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

		/** Advance the simulation by DeltaTime */
		void Simulate(float DeltaTime, float MaxStepTime, int32 MaxSubSteps, const FVector& InGravity);
		void Simulate_AssumesLocked(float DeltaTime, float MaxStepTime, int32 MaxSubSteps, const FVector& InGravity) { Simulate(DeltaTime, MaxStepTime, MaxSubSteps, InGravity); }

		void SetSimulationSpaceTransform(const FTransform& Transform);

		/** Set new iteration counts. A negative value with leave that iteration count unchanged */
		void SetSolverIterations(const FReal FixedDt, const int32 SolverIts, const int32 JointIts, const int32 CollisionIts, const int32 SolverPushOutIts, const int32 JointPushOutIts, const int32 CollisionPushOutIts);

	private:
		void UpdateActivePotentiallyCollidingPairs();
		FReal UpdateStepTime(const FReal DeltaTime, const FReal MaxStepTime);

		void DebugDrawKinematicParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color);
		void DebugDrawDynamicParticles(const int32 MinDebugLevel, const int32 MaxDebugLevel, const FColor& Color);
		void DebugDrawConstraints(const int32 MinDebugLevel, const int32 MaxDebugLevel, const float ColorScale);

		struct FImplementation;
		TUniquePtr<FImplementation> Implementation;
	};

}
