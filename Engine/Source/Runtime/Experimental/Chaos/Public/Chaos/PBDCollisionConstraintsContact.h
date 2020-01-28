// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/CollisionResolutionTypes.h"

namespace Chaos
{
	namespace Collisions
	{
		template<class T = float>
		struct TContactParticleParameters {
			T CullDistance;
			T ShapePadding;
			TArrayCollectionArray<bool>* Collided;
		};

		template<class T = float>
		struct TContactIterationParameters {
			const T Dt;
			const int32 Iteration;
			const int32 NumIterations;
			const int32 NumPairIterations;
			bool* NeedsAnotherIteration;
		};

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void Update(const T CullDistance, TCollisionConstraintBase<T, d>& Constraint);

		template<typename T, int d>
		void UpdateManifold(const T CullDistance, TCollisionConstraintBase<T, d>& Constraint);

		template<typename T, int d>
		void Apply(TCollisionConstraintBase<T, d>& Constraint, const TContactIterationParameters<T> & IterationParameters, const TContactParticleParameters<T> & ParticleParameters);

		template<typename T, int d>
		void ApplyPushOut(TCollisionConstraintBase<T, d>& Constraint, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic,
			const TContactIterationParameters<T> & IterationParameters, const TContactParticleParameters<T> & ParticleParameters);


	}

}
