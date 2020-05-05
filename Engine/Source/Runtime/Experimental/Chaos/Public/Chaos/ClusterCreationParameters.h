// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ParticleHandle.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/Defines.h"

namespace Chaos
{
	template<class T, int d>
	class TBVHParticles;

	template <typename T>
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
			T CoillisionThicknessPercentIn = 0.3
			, int32 MaxNumConnectionsIn = 100
			, bool bCleanCollisionParticlesIn = true
			, bool bCopyCollisionParticlesIn = true
			, bool bGenerateConnectionGraphIn = true, EConnectionMethod ConnectionMethodIn = EConnectionMethod::MinimalSpanningSubsetDelaunayTriangulation
			, TBVHParticles<float, 3>* CollisionParticlesIn = nullptr
			, Chaos::TPBDRigidClusteredParticleHandle<float,3>* ClusterParticleHandleIn = nullptr
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

		T CoillisionThicknessPercent;
		int32 MaxNumConnections;
		bool bCleanCollisionParticles;
		bool bCopyCollisionParticles;
		bool bGenerateConnectionGraph;
		EConnectionMethod ConnectionMethod;
		TBVHParticles<T, 3>* CollisionParticles;
		Chaos::TPBDRigidClusteredParticleHandle<float, 3>* ClusterParticleHandle;
	};
}
