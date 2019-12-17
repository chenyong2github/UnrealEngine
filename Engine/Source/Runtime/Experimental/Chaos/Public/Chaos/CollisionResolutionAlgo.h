// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/PBDCollisionTypes.h"
#include "Chaos/Pair.h"

namespace Chaos
{

	template <typename T, int d>
	class TBox;

	template <typename T>
	class TCapsule;

	template <typename T, int d>
	class TPlane;

	class FImplicitObject;

	template<typename T, int d>
	class TRigidBodySingleContactConstraint;

	template <typename T, int d>
	class TRigidTransform;

	template <typename T, int d>
	class TSphere;

	namespace Collisions
	{
		//
		// Utility
		//
		template <typename T, int d>
		TArray<Pair<const FImplicitObject*, TRigidTransform<T, d>>> CHAOS_API FindRelevantShapes2(const FImplicitObject* ParticleObj, const TRigidTransform<T, d>& ParticlesTM, const FImplicitObject& LevelsetObj, const TRigidTransform<T, d>& LevelsetTM, const T Thickness);

		//
		// Construct Constraints
		//
		template<typename T, int d>
		void CHAOS_API ConstructLevelsetConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructBoxPlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructSphereConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructSpherePlaneConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructSphereBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructCapsuleCapsuleConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructCapsuleBoxConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructSingleUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructUnionUnionConstraints(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructPairConstraintImpl(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<typename T, int d>
		void CHAOS_API ConstructConstraintsImpl(TGeometryParticleHandle<T, d>* Particle0, TGeometryParticleHandle<T, d>* Particle1, const FImplicitObject* Implicit0, const FImplicitObject* Implicit1, const T Thickness, TRigidBodySingleContactConstraint<T, d> & Constraint);


		//
		// Update Constraints
		//
		template<ECollisionUpdateType UpdateType, typename T = float, int d = 3>
		void CHAOS_API UpdateLevelsetConstraint(const T Thickness, TRigidBodySingleContactConstraint<float, 3>& Constraint);

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void CHAOS_API UpdateConstraintImp(const FImplicitObject& ParticleObject, const TRigidTransform<T, d>& ParticleTM, const FImplicitObject& LevelsetObject, const TRigidTransform<T, d>& LevelsetTM, const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template <typename T, int d>
		bool CHAOS_API UpdateBoxPlaneConstraint(const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template <typename T, int d>
		void CHAOS_API UpdateSphereConstraint(const TSphere<T, d>& Sphere1, const TRigidTransform<T, d>& Sphere1Transform, const TSphere<T, d>& Sphere2, const TRigidTransform<T, d>& Sphere2Transform, T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template <typename T, int d>
		void CHAOS_API UpdateSpherePlaneConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TPlane<T, d>& Plane, const TRigidTransform<T, d>& PlaneTransform, T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template <typename T, int d>
		void CHAOS_API UpdateSphereBoxConstraint(const TSphere<T, d>& Sphere, const TRigidTransform<T, d>& SphereTransform, const TBox<T, d>& Box, const TRigidTransform<T, d>& BoxTransform, T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template <typename T, int d>
		void CHAOS_API UpdateCapsuleCapsuleConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TCapsule<T>& B, const TRigidTransform<T, d>& BTransform, T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template <typename T, int d>
		void CHAOS_API UpdateCapsuleBoxConstraint(const TCapsule<T>& A, const TRigidTransform<T, d>& ATransform, const TBox<T, d>& B, const TRigidTransform<T, d>& BTransform, T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void CHAOS_API UpdateUnionUnionConstraint(const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void CHAOS_API UpdateSingleUnionConstraint(const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void CHAOS_API UpdateUnionLevelsetConstraint(const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template<ECollisionUpdateType UpdateType, typename T, int d>
		void CHAOS_API UpdateLevelsetUnionConstraint(const T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

		template <typename T, int d>
		void CHAOS_API UpdateBoxConstraint(const TBox<T, d>& Box1, const TRigidTransform<T, d>& Box1Transform, const TBox<T, d>& Box2, const TRigidTransform<T, d>& Box2Transform, T Thickness, TRigidBodySingleContactConstraint<T, d>& Constraint);

	}// Collisions

} // Chaos
