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
			FPBDCollisionConstraint& Constraint,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters);

		void ApplyPushOutManifold(
			FPBDCollisionConstraint& Constraint,
			const FContactIterationParameters& IterationParameters,
			const FContactParticleParameters& ParticleParameters);

	}
}
