// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

namespace Chaos
{
	template<class T, int d>
	class TBVHParticles;

	template <typename T>
	struct FClusterCreationParameters
	{
		enum EConnectionMethod { None = 0, PointImplicit, DelaunayTriangulation, MinimalSpanningSubsetDelaunayTriangulation, PointImplicitAugmentedWithMinimalDelaunay };


		FClusterCreationParameters(
			T CoillisionThicknessPercentIn = 0.3
			, int32 MaxNumConnectionsIn = 100
			, bool bCleanCollisionParticlesIn = true
			, bool bCopyCollisionParticlesIn = true
			, bool bGenerateConnectionGraphIn = true
			, EConnectionMethod ConnectionMethodIn = EConnectionMethod::PointImplicitAugmentedWithMinimalDelaunay
			, TBVHParticles<float, 3>* CollisionParticlesIn = nullptr
			, int32 RigidBodyIndexIn = INDEX_NONE
		)
			: CoillisionThicknessPercent(CoillisionThicknessPercentIn)
			, MaxNumConnections(MaxNumConnectionsIn)
			, bCleanCollisionParticles(bCleanCollisionParticlesIn)
			, bCopyCollisionParticles(bCopyCollisionParticlesIn)
			, bGenerateConnectionGraph(bGenerateConnectionGraphIn)
			, ConnectionMethod(ConnectionMethodIn)
			, CollisionParticles(CollisionParticlesIn)
			, RigidBodyIndex(RigidBodyIndexIn)
		{}

		T CoillisionThicknessPercent;
		int32 MaxNumConnections;
		bool bCleanCollisionParticles;
		bool bCopyCollisionParticles;
		bool bGenerateConnectionGraph;
		EConnectionMethod ConnectionMethod;
		TBVHParticles<float, 3>* CollisionParticles;
		int32 RigidBodyIndex;
	};
}
