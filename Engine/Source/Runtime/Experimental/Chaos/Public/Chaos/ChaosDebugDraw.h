// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/ChaosDebugDrawDeclares.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	namespace DebugDraw
	{
#if CHAOS_DEBUG_DRAW
		CHAOS_API void DrawParticleShapes(const TRigidTransform<float, 3>& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, float ColorScale, bool bDrawKinematic = true, bool bDrawDynamic = true);
		CHAOS_API void DrawParticleShapes(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TGeometryParticleHandle<float, 3>*>& Particles, float ColorScale, bool bDrawKinematic = true, bool bDrawDynamic = true);
		CHAOS_API void DrawParticleTransforms(const TRigidTransform<float, 3>& SpaceTransform, const TParticleView<TGeometryParticles<float, 3>>& ParticlesView, float ColorScale, bool bDrawKinematic = true, bool bDrawDynamic = true);
		CHAOS_API void DrawParticleTransforms(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TGeometryParticleHandle<float, 3>*>& Particles, float ColorScale, bool bDrawKinematic = true, bool bDrawDynamic = true);
		CHAOS_API void DrawParticleCollisions(const TRigidTransform<float, 3>& SpaceTransform, const TGeometryParticleHandle<float, 3>* Particle, const TPBDCollisionConstraint<float, 3>& Collisions);
		CHAOS_API void DrawCollisions(const TRigidTransform<float, 3>& SpaceTransform, const TPBDCollisionConstraint<float, 3>& Collisions, float ColorScale);
		CHAOS_API void DrawCollisions(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TPBDCollisionConstraintHandle<float, 3>*>& ConstraintHandles, float ColorScale);
		CHAOS_API void DrawJointConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TPBDJointConstraintHandle<float, 3>*>& ConstraintHandles, float ColorScale);
		CHAOS_API void DrawJointConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TPBDJointConstraints<float, 3>& Constraints, float ColorScale);
		CHAOS_API void Draw6DofConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TArray<TPBD6DJointConstraintHandle<float, 3>*>& ConstraintHandles, float ColorScale);
		CHAOS_API void Draw6DofConstraints(const TRigidTransform<float, 3>& SpaceTransform, const TPBD6DJointConstraints<float, 3>& Constraints, float ColorScale);

		extern CHAOS_API float ConstraintAxisLen;
		extern CHAOS_API float BodyAxisLen;
		extern CHAOS_API float ArrowSize;
		extern CHAOS_API float LineThickness;
#endif
	}
}
