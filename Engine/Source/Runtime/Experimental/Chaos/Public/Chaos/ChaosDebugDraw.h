// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Declares.h"
#include "Chaos/ChaosDebugDrawDeclares.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	namespace DebugDraw
	{
#if CHAOS_DEBUG_DRAW

		struct CHAOS_API FChaosDebugDrawSettings
		{
		public:
			FChaosDebugDrawSettings(
				FRealSingle InArrowSize,
				FRealSingle InBodyAxisLen,
				FRealSingle InContactLen,
				FRealSingle InContactWidth,
				FRealSingle InContactPhiWidth,
				FRealSingle InContactOwnerWidth,
				FRealSingle InConstraintAxisLen,
				FRealSingle InJointComSize,
				FRealSingle InLineThickness,
				FRealSingle InDrawScale,
				FRealSingle InFontHeight,
				FRealSingle InFontScale,
				FRealSingle InShapeThicknesScale,
				FRealSingle InPointSize,
				FRealSingle InVelScale,
				FRealSingle InAngVelScale,
				FRealSingle InImpulseScale,
				int InDrawPriority
			)
				: ArrowSize(InArrowSize)
				, BodyAxisLen(InBodyAxisLen)
				, ContactLen(InContactLen)
				, ContactWidth(InContactWidth)
				, ContactPhiWidth(InContactPhiWidth)
				, ContactOwnerWidth(InContactOwnerWidth)
				, ConstraintAxisLen(InConstraintAxisLen)
				, JointComSize(InJointComSize)
				, LineThickness(InLineThickness)
				, DrawScale(InDrawScale)
				, FontHeight(InFontHeight)
				, FontScale(InFontScale)
				, ShapeThicknesScale(InShapeThicknesScale)
				, PointSize(InPointSize)
				, VelScale(InVelScale)
				, AngVelScale(InAngVelScale)
				, ImpulseScale(InImpulseScale)
				, DrawPriority(InDrawPriority)
			{}

			FRealSingle ArrowSize;
			FRealSingle BodyAxisLen;
			FRealSingle ContactLen;
			FRealSingle ContactWidth;
			FRealSingle ContactPhiWidth;
			FRealSingle ContactOwnerWidth;
			FRealSingle ConstraintAxisLen;
			FRealSingle JointComSize;
			FRealSingle LineThickness;
			FRealSingle DrawScale;
			FRealSingle FontHeight;
			FRealSingle FontScale;
			FRealSingle ShapeThicknesScale;
			FRealSingle PointSize;
			FRealSingle VelScale;
			FRealSingle AngVelScale;
			FRealSingle ImpulseScale;
			int DrawPriority;
		};


		enum class EDebugDrawJointFeature
		{
			None = 0,
			CoMConnector = 1 << 0,		// 1
			ActorConnector = 1 << 1,	// 2
			Stretch = 1 << 2,			// 4
			Axes = 1 << 3,				// 8
			Level = 1 << 4,				// 16
			Index = 1 << 5,				// 32
			Color = 1 << 6,				// 64
			Batch = 1 << 7,				// 128
			Island = 1 << 8,			// 2

			Default = ActorConnector | Stretch
		};

		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* Particle, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleShapes(const FRigidTransform3& SpaceTransform, const FGeometryParticle* Particle, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleBounds(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, const FReal Dt, const FReal BoundsThickness, const FReal BoundsThicknessVelocityInflation, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<FGeometryParticles>& ParticlesView, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<FKinematicGeometryParticles>& ParticlesView, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleTransforms(const FRigidTransform3& SpaceTransform, const TParticleView<FPBDRigidParticles>& ParticlesView, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawParticleCollisions(const FRigidTransform3& SpaceTransform, const FGeometryParticleHandle* Particle, const FPBDCollisionConstraints& Collisions, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawCollisions(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, FRealSingle ColorScale, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawCollisions(const FRigidTransform3& SpaceTransform, const TArray<FPBDCollisionConstraintHandle*>& ConstraintHandles, FRealSingle ColorScale, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const TArray<FPBDJointConstraintHandle*>& ConstraintHandles, FRealSingle ColorScale, uint32 FeatureMask = (uint32)EDebugDrawJointFeature::Default, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const FPBDJointConstraints& Constraints, FRealSingle ColorScale, uint32 FeatureMask = (uint32)EDebugDrawJointFeature::Default, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawSimulationSpace(const FSimulationSpace& SimSpace, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawShape(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawConstraintGraph(const FRigidTransform3& ShapeTransform, const FPBDConstraintColor& Graph, const FChaosDebugDrawSettings* Settings = nullptr);
#endif
	}
}
