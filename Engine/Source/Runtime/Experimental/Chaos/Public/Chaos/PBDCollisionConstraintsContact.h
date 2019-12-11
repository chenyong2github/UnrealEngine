// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
			T Thickness;
			TArrayCollectionArray<bool>* Collided;
			const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>* PhysicsMaterials;
			T FrictionOverride;
			T AngularFrictionOverride;
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
		void Update(const T Thickness, TCollisionConstraintBase<T, d>& Constraint);

		template<typename T, int d>
		void UpdateManifold(const T Thickness, TCollisionConstraintBase<T, d>& Constraint);

		template<typename T, int d>
		void Apply(TCollisionConstraintBase<T, d>& Constraint, TContactIterationParameters<T> & IterationParameters, TContactParticleParameters<T> & ParticleParameters);

		template<typename T, int d>
		void ApplyPushOut(TCollisionConstraintBase<T, d>& Constraint, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic,
			TContactIterationParameters<T> & IterationParameters, TContactParticleParameters<T> & ParticleParameters);


	}

}
