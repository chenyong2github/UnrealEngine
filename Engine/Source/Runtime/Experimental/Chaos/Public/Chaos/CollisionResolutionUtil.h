// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Pair.h"

namespace Chaos
{
	class FImplicitObject;

	template<typename T, int d>
	class TRigidBodyPointContactConstraint;

	template <typename T, int d>
	class TRigidTransform;

	namespace Collisions
	{
		//
		// Utility
		//

		template<typename T = float, int d = 3>
		TRigidTransform<T, d> 
		GetTransform(const TGeometryParticleHandle<T, d>* Particle);

		template<typename T>
		PMatrix<T, 3, 3> 
		ComputeFactorMatrix3(const TVector<T, 3>& V, const PMatrix<T, 3, 3>& M, const T& Im);

		template<typename T, int d>
		TVector<T, d> 
		GetEnergyClampedImpulse(const TPBDRigidParticleHandle<T, d>* PBDRigid0, const TPBDRigidParticleHandle<T, d>* PBDRigid1, const TVector<T, d>& Impulse, const TVector<T, d>& VectorToPoint1, const TVector<T, d>& VectorToPoint2, const TVector<T, d>& Velocity1, const TVector<T, d>& Velocity2);

		template <typename T, int d>
		bool 
		SampleObjectHelper(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);

		template <typename T, int d>
		bool 
		SampleObjectNoNormal(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);

		template <typename T, int d>
		bool 
		SampleObjectNormalAverageHelper(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TRigidTransform<T, d>& SampleToObjectTransform, const TVector<T, d>& SampleParticle, T Thickness, T& TotalThickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);

#if INTEL_ISPC
		template<ECollisionUpdateType UpdateType>
		void 
		SampleObject(const FImplicitObject& Object, const TRigidTransform<float, 3>& ObjectTransform, const TBVHParticles<float, 3>& SampleParticles, const TRigidTransform<float, 3>& SampleParticlesTransform, float Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
#else
		template <ECollisionUpdateType UpdateType, typename T, int d>
		void
		SampleObject(const FImplicitObject& Object, const TRigidTransform<T, d>& ObjectTransform, const TBVHParticles<T, d>& SampleParticles, const TRigidTransform<T, d>& SampleParticlesTransform, T Thickness, TRigidBodyPointContactConstraint<float, 3>& Constraint);
#endif

		template <typename T, int d>
		TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> 
		FindRelevantShapes(const FImplicitObject* ParticleObj, const TRigidTransform<T, d>& ParticlesTM, const FImplicitObject& LevelsetObj, const TRigidTransform<T, d>& LevelsetTM, const T Thickness);

	}// Collisions

} // Chaos
