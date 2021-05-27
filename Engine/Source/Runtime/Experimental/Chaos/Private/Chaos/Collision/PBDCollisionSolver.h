// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Defines.h"

namespace Chaos
{
	namespace Collisions
	{
		struct FContactIterationParameters;
		struct FContactParticleParameters;
	}
	class FRigidBodyPointContactConstraint;

	class FPBDCollisionSolver
	{
	public:
		void SolvePosition(
			FRigidBodyPointContactConstraint& Constraint,
			const Collisions::FContactIterationParameters& IterationParameters,
			const Collisions::FContactParticleParameters& ParticleParameters);

		void SolveVelocity(
			FRigidBodyPointContactConstraint& Constraint,
			const Collisions::FContactIterationParameters& IterationParameters,
			const Collisions::FContactParticleParameters& ParticleParameters);
	};
}