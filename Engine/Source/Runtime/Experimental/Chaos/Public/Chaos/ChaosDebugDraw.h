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
				int InDrawPriority,
				bool bInShowSimpleCollision,
				bool bInShowComplexCollision,
				bool bInShowLevelSetCollision
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
				, bShowSimpleCollision(bInShowSimpleCollision)
				, bShowComplexCollision(bInShowComplexCollision)
				, bShowLevelSetCollision(bInShowLevelSetCollision)
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
			bool bShowSimpleCollision;
			bool bShowComplexCollision;
			bool bShowLevelSetCollision;
		};

		// A bitmask of features to show when drawing joints
		class FChaosDebugDrawJointFeatures
		{
		public:
			FChaosDebugDrawJointFeatures()
				: bCoMConnector(false)
				, bActorConnector(false)
				, bStretch(false)
				, bAxes(false)
				, bLevel(false)
				, bIndex(false)
				, bColor(false)
				, bBatch(false)
				, bIsland(false)
			{}

			static FChaosDebugDrawJointFeatures MakeEmpty()
			{
				return FChaosDebugDrawJointFeatures();
			}

			static FChaosDebugDrawJointFeatures MakeDefault()
			{
				FChaosDebugDrawJointFeatures Features = FChaosDebugDrawJointFeatures();
				Features.bActorConnector = true;
				Features.bStretch = true;
				return Features;
			}

			bool bCoMConnector;
			bool bActorConnector;
			bool bStretch;
			bool bAxes;
			bool bLevel;
			bool bIndex;
			bool bColor;
			bool bBatch;
			bool bIsland;
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
		CHAOS_API void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const TArray<FPBDJointConstraintHandle*>& ConstraintHandles, FRealSingle ColorScale, const FChaosDebugDrawJointFeatures& FeatureMask = FChaosDebugDrawJointFeatures::MakeDefault(), const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawJointConstraints(const FRigidTransform3& SpaceTransform, const FPBDJointConstraints& Constraints, FRealSingle ColorScale, const FChaosDebugDrawJointFeatures& FeatureMask = FChaosDebugDrawJointFeatures::MakeDefault(), const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawSimulationSpace(const FSimulationSpace& SimSpace, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawShape(const FRigidTransform3& ShapeTransform, const FImplicitObject* Shape, const FColor& Color, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawConstraintGraph(const FRigidTransform3& ShapeTransform, const FPBDConstraintColor& Graph, const FChaosDebugDrawSettings* Settings = nullptr);
		CHAOS_API void DrawCollidingShapes(const FRigidTransform3& SpaceTransform, const FPBDCollisionConstraints& Collisions, float ColorScale, const FChaosDebugDrawSettings* Settings = nullptr);
#endif
	}
}
