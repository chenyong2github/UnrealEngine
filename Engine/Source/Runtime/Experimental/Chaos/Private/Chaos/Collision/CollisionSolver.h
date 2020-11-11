// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"

namespace Chaos
{
	namespace Collisions
	{
		void ApplyContactManifold(
			FRigidBodyPointContactConstraint& Constraint,
			TGenericParticleHandle<FReal, 3> Particle0,
			TGenericParticleHandle<FReal, 3> Particle1,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters);

		void ApplyPushOutManifold(
			FRigidBodyPointContactConstraint& Constraint,
			const TSet<const TGeometryParticleHandle<FReal, 3>*>& IsTemporarilyStatic,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters,
			const FVec3& GravityDir);

	}
}
