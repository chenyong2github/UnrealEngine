// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDCollisionConstraintsPlaneContactUtil.h"
#include "Chaos/CollisionResolutionUtil.h"
#include "Chaos/Defines.h"

namespace Chaos
{
	namespace Collisions
	{
		template<ECollisionUpdateType UpdateType, typename T, int d>
		void Update(const T Thickness, TRigidBodyPlaneContactConstraint<T, d>& Constraint)
		{
		}


		template<typename T, int d>
		void Apply(TRigidBodyPlaneContactConstraint<T, d>& Constraint, T Thickness, TPlaneContactIterationParameters<T> & IterationParameters, TPlaneContactParticleParameters<T> & ParticleParameters)
		{
		}

		template<typename T, int d>
		void ApplyPushOut(TRigidBodyPlaneContactConstraint<T, d>& Constraint, T Thickness, const TSet<const TGeometryParticleHandle<T, d>*>& IsTemporarilyStatic,
			TPlaneContactIterationParameters<T> & IterationParameters, TPlaneContactParticleParameters<T> & ParticleParameters)
		{
		}


		template void Update<ECollisionUpdateType::Any, float, 3>(const float, TRigidBodyPlaneContactConstraint<float, 3>&);
		template void Update<ECollisionUpdateType::Deepest, float, 3>(const float, TRigidBodyPlaneContactConstraint<float, 3>&);
		template void Apply<float, 3>(TRigidBodyPlaneContactConstraint<float, 3>&, float, TPlaneContactIterationParameters<float> &, TPlaneContactParticleParameters<float> &);
		template void ApplyPushOut<float, 3>(TRigidBodyPlaneContactConstraint<float,3>&, float , const TSet<const TGeometryParticleHandle<float,3>*>&,
			TPlaneContactIterationParameters<float> & IterationParameters, TPlaneContactParticleParameters<float> & ParticleParameters);

	} // Collisions

}// Chaos

