// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/NarrowPhase.h"
#include "Chaos/Collision/ParticlePairBroadPhase.h"
#include "Chaos/Collision/SpatialAccelerationBroadPhase.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace Chaos
{
	DECLARE_CYCLE_STAT_EXTERN(TEXT("Collisions::Detect"), STAT_Collisions_Detect, STATGROUP_ChaosCollision, CHAOS_API);

	template<typename T_BROADPHASE, typename T_NARROWPHASE, typename T_RECEIVER, typename T_CONTAINER>
	class CHAOS_API TCollisionDetector
	{
	public:
		using FBroadPhase = T_BROADPHASE;
		using FReceiver = T_RECEIVER;
		using FNarrowPhase = T_NARROWPHASE;
		using FContainer = T_CONTAINER;

		TCollisionDetector(FBroadPhase& InBroadPhase, FContainer& InCollisionContainer)
			: BroadPhase(InBroadPhase)
			, CollisionContainer(InCollisionContainer)
		{
		}

		FBroadPhase& GetBroadPhase() { return BroadPhase; }
		FContainer& GetCollisionContainer() { return CollisionContainer; }

		void DetectCollisions(const FReal Dt)
		{
			CollisionStats::FStatData StatData(false);
			DetectCollisions(Dt, StatData);
		}

		void DetectCollisions(const FReal Dt, CollisionStats::FStatData& StatData)
		{
			SCOPE_CYCLE_COUNTER(STAT_Collisions_Detect);
			CHAOS_SCOPED_TIMER(DetectCollisions);

			if (!GetCollisionContainer().GetCollisionsEnabled())
			{
				return;
			}

			CollisionContainer.UpdateManifolds(Dt);
			CollisionContainer.UpdateConstraints(Dt);

			// Collision detection pipeline: BroadPhase -> NarrowPhase -> Receiver -> Container
			// Receivers and NarrowPhase are assumed to be stateless atm. If we change that, they need to
			// be passed into the constructor with the BroadPhase and Container.
			FReceiver Receiver(CollisionContainer);
			FNarrowPhase NarrowPhase;
			BroadPhase.ProduceOverlaps(Dt, NarrowPhase, Receiver, StatData);
			Receiver.ProcessCollisions();
		}

	private:
		FBroadPhase& BroadPhase;
		FContainer& CollisionContainer;
	};

}
