// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Pair.h"

namespace Chaos
{
	template<class T, int d>
	class TBVHParticles;

	class FImplicitObject;
	class FRigidBodyPointContactConstraint;

	namespace Collisions
	{
		//
		// Utility
		//

		FRigidTransform3 
		GetTransform(const TGeometryParticleHandle<FReal, 3>* Particle);

		FMatrix33 
		ComputeFactorMatrix3(const FVec3& V, const FMatrix33& M, const FReal Im);

		FVec3
		GetEnergyClampedImpulse(const TPBDRigidParticleHandle<FReal, 3>* PBDRigid0, const TPBDRigidParticleHandle<FReal, 3>* PBDRigid1, const FVec3& Impulse, const FVec3& VectorToPoint1, const FVec3& VectorToPoint2, const FVec3& Velocity1, const FVec3& Velocity2);

		bool 
		SampleObjectHelper(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const FRigidTransform3& SampleToObjectTransform, const FVec3& SampleParticle, FReal Thickness, FRigidBodyPointContactConstraint& Constraint);

		bool 
		SampleObjectNoNormal(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const FRigidTransform3& SampleToObjectTransform, const FVec3& SampleParticle, FReal Thickness, FRigidBodyPointContactConstraint& Constraint);

		bool 
		SampleObjectNormalAverageHelper(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const FRigidTransform3& SampleToObjectTransform, const FVec3& SampleParticle, FReal Thickness, FReal& TotalThickness, FRigidBodyPointContactConstraint& Constraint);

		template <ECollisionUpdateType UpdateType>
		void
		SampleObject(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const TBVHParticles<FReal, 3>& SampleParticles, const FRigidTransform3& SampleParticlesTransform, FReal Thickness, FRigidBodyPointContactConstraint& Constraint);

		TArray<Pair<const FImplicitObject*, FRigidTransform3>> 
		FindRelevantShapes(const FImplicitObject* ParticleObj, const FRigidTransform3& ParticlesTM, const FImplicitObject& LevelsetObj, const FRigidTransform3& LevelsetTM, const FReal Thickness);

	}// Collisions

} // Chaos
