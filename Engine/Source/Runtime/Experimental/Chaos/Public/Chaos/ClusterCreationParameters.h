// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ParticleHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Defines.h"

namespace Chaos
{
	class FBVHParticles;

	struct CHAOS_API FClusterCreationParameters
	{
		enum EConnectionMethod
		{
			PointImplicit = 0,
			DelaunayTriangulation,
			MinimalSpanningSubsetDelaunayTriangulation,
			PointImplicitAugmentedWithMinimalDelaunay,
			None
		};


		FClusterCreationParameters(
			FReal CoillisionThicknessPercentIn = (FReal)0.3
			, int32 MaxNumConnectionsIn = 100
			, bool bCleanCollisionParticlesIn = true
			, bool bCopyCollisionParticlesIn = true
			, bool bGenerateConnectionGraphIn = true, EConnectionMethod ConnectionMethodIn = EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation
			, FBVHParticles* CollisionParticlesIn = nullptr
			, Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal,3>* ClusterParticleHandleIn = nullptr
		)
			: CoillisionThicknessPercent(CoillisionThicknessPercentIn)
			, MaxNumConnections(MaxNumConnectionsIn)
			, bCleanCollisionParticles(bCleanCollisionParticlesIn)
			, bCopyCollisionParticles(bCopyCollisionParticlesIn)
			, bGenerateConnectionGraph(bGenerateConnectionGraphIn)
			, ConnectionMethod(ConnectionMethodIn)
			, CollisionParticles(CollisionParticlesIn)
			, ClusterParticleHandle(ClusterParticleHandleIn)
		{}

		FReal CoillisionThicknessPercent;
		int32 MaxNumConnections;
		bool bCleanCollisionParticles;
		bool bCopyCollisionParticles;
		bool bGenerateConnectionGraph;
		EConnectionMethod ConnectionMethod;
		FBVHParticles* CollisionParticles;
		Chaos::FPBDRigidClusteredParticleHandle* ClusterParticleHandle;
	};
}
