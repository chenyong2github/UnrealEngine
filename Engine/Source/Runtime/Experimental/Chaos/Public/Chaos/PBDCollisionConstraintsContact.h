// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"

namespace Chaos
{
	class FCollisionContext;

	namespace Collisions
	{
		struct FContactParticleParameters 
		{
			FContactParticleParameters()
				: RestitutionVelocityThreshold(0)
				, bCanDisableContacts(false)
				, Collided(nullptr)
			{}

			FContactParticleParameters(
				FReal InRestitutionVelocityThreshold,
				bool bInCanDisableContacts,
				TArrayCollectionArray<bool>* InCollided)
				: RestitutionVelocityThreshold(InRestitutionVelocityThreshold)
				, bCanDisableContacts(bInCanDisableContacts)
				, Collided(InCollided)
			{}


			FReal RestitutionVelocityThreshold;
			bool bCanDisableContacts;
			TArrayCollectionArray<bool>* Collided;
		};

		struct FContactIterationParameters 
		{
			FContactIterationParameters()
				: Dt(0)
				, Iteration(0)
				, NumIterations(0)
				, NumPairIterations(0)
				, ApplyType(ECollisionApplyType::None)
				, NeedsAnotherIteration(nullptr)
			{}

			FContactIterationParameters(
				const FReal InDt,
				const int32 InIteration,
				const int32 InNumIterations,
				const int32 InNumPairIterations,
				const ECollisionApplyType InApplyType,
				bool* InNeedsAnotherIteration)
				: Dt(InDt)
				, Iteration(InIteration)
				, NumIterations(InNumIterations)
				, NumPairIterations(InNumPairIterations)
				, ApplyType(InApplyType)
				, NeedsAnotherIteration(InNeedsAnotherIteration)
			{}

			const FReal Dt;
			const int32 Iteration;
			const int32 NumIterations;
			const int32 NumPairIterations;
			const ECollisionApplyType ApplyType;	// @todo(chaos): a better way to customize the collision solver
			bool* NeedsAnotherIteration;
		};

		// Update the constraint (re-runs collision detection for this contact)
		extern void Update(FRigidBodyPointContactConstraint& Constraint, const FReal Dt);

		extern void Apply(FCollisionConstraintBase& Constraint, const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters);

		extern void ApplyPushOut(FCollisionConstraintBase& Constraint, const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters, const FContactParticleParameters& ParticleParameters, const FVec3& GravityDir = FVec3::DownVector);


	}

}
