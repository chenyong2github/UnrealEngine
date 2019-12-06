// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDRigidsSOAs.h"

namespace Chaos
{
	/**
	 * Run through a list of particle pairs and pass them onto the collision detector if their AABBs overlap.
	 * No spatial acceleration, and the order is assumed to be already optimized for cache efficiency.
	 */
	class CHAOS_API FParticlePairBroadPhase
	{
	public:
		using FParticlePair = TVector<TGeometryParticleHandle<FReal, 3>*, 2>;
		using FAABB = TAABB<FReal, 3>;

		FParticlePairBroadPhase(const TArray<FParticlePair> InParticlePairs)
			: ParticlePairs(InParticlePairs)
		{
		}

		/**
		 *
		 */
		void ProduceOverlaps(FReal Dt,
			FNarrowPhase& NarrowPhase,
			FSyncCollisionReceiver& Receiver,
			CollisionStats::FStatData& StatData)
		{
			// @todo(ccaulfield)
		}

	private:
		TArray<FParticlePair> ParticlePairs;
	};
}
