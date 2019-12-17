// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ExternalCollisionData.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/CollisionResolutionTypes.h"

namespace Chaos
{
	namespace Collisions
	{
		template<ECollisionUpdateType UpdateType, typename T = float, int d = 3>
		void Update(const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<ECollisionUpdateType UpdateType, typename T=float, int d=3>
		void UpdateConstraint(const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);
			
		template<typename T = float, int d = 3>
		void Apply(TRigidBodySingleContactConstraint<T, d>& Constraint, T Thickness, 
			const T Dt, const int32 It, const int32 NumIts, const int32 NumPairIterations,
			TArrayCollectionArray<bool>* Collided, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>*,T CollisionFrictionOverride, T AngularFriction);
			
		template<typename T=float, int d=3>
		void ApplyPushOut(TRigidBodySingleContactConstraint<T, d>& Constraint, T Thickness, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic, 
			const T Dt, const int32 Iteration, const int32 NumIterations, const int32 NumPairIterations, bool &NeedsAnotherIteration,
			const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>* PhysicsMaterials);

	}

}
