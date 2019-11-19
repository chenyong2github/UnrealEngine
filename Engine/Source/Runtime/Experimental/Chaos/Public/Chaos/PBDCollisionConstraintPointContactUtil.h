// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/CollisionResolutionTypes.h"

namespace Chaos
{
	namespace Collisions
	{
		template<class T = float>
		struct TPointContactParticleParameters {
			TArrayCollectionArray<bool>* Collided;
			const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>* PhysicsMaterials;
			T FrictionOverride;
			T AngularFrictionOverride;
		};

		template<class T = float>
		struct TPointContactIterationParameters {
			const T Dt; 
			const int32 Iteration; 
			const int32 NumIterations; 
			const int32 NumPairIterations;
			bool* NeedsAnotherIteration;
		};

		template<ECollisionUpdateType UpdateType, typename T = float, int d = 3>
		void Update(const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint);

		template<ECollisionUpdateType UpdateType, typename T=float, int d=3>
		void UpdateConstraint(const T Thickness, TRigidBodyPointContactConstraint<T, d>& Constraint);
			
		template<typename T = float, int d = 3>
		void Apply(TRigidBodyPointContactConstraint<T, d>& Constraint, T Thickness, 
			TPointContactIterationParameters<T> & IterationParameters,
			TPointContactParticleParameters<T> & ParticleParameters);
			
		template<typename T=float, int d=3>
		void ApplyPushOut(TRigidBodyPointContactConstraint<T, d>& Constraint, T Thickness, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic,
			TPointContactIterationParameters<T> & IterationParameters,
			TPointContactParticleParameters<T> & ParticleParameters);
	}

}
