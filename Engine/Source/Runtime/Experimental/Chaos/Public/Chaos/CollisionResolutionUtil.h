// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Pair.h"

namespace Chaos
{
	class FBVHParticles;
	class FContactPoint;
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

		FVec3 GetEnergyClampedImpulse(
			const FVec3& Impulse,
			FReal InvM0,
			const FMatrix33& InvI0,
			FReal InvM1,
			const FMatrix33& InvI1,
			const FRotation3& Q0,
			const FVec3& V0,
			const FVec3& W0,
			const FRotation3& Q1,
			const FVec3& V1,
			const FVec3& W1,
			const FVec3& ContactOffset0,
			const FVec3& ContactOffset1,
			const FVec3& ContactVelocity0,
			const FVec3& ContactVelocity1);

		bool 
		SampleObjectHelper(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const FRigidTransform3& SampleToObjectTransform, const FVec3& SampleParticle, FReal Thickness, FContactPoint& ContactPoint);

		bool 
		SampleObjectNoNormal(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const FRigidTransform3& SampleToObjectTransform, const FVec3& SampleParticle, FReal Thickness, FContactPoint& ContactPoint);

		bool 
		SampleObjectNormalAverageHelper(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const FRigidTransform3& SampleToObjectTransform, const FVec3& SampleParticle, FReal Thickness, FReal& TotalThickness, FContactPoint& ContactPoint);

		template <ECollisionUpdateType UpdateType>
		FContactPoint
		SampleObject(const FImplicitObject& Object, const FRigidTransform3& ObjectTransform, const FBVHParticles& SampleParticles, const FRigidTransform3& SampleParticlesTransform, FReal Thickness);

		TArray<Pair<const FImplicitObject*, FRigidTransform3>> 
		FindRelevantShapes(const FImplicitObject* ParticleObj, const FRigidTransform3& ParticlesTM, const FImplicitObject& LevelsetObj, const FRigidTransform3& LevelsetTM, const FReal Thickness);

	}// Collisions

} // Chaos
