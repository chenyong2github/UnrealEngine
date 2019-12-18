// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/BoundingVolumeUtilities.h"
#include "Chaos/Collision/CollisionReceiver.h"
#include "Chaos/Collision/NarrowPhase.h"
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

		FParticlePairBroadPhase(const TArray<FParticlePair>& InParticlePairs)
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
			const FReal BoundsThickness = 1.0f;
			const FReal BoundsThicknessVelocityInflation = 2.0f;

			for (const FParticlePair& ParticlePair : ParticlePairs)	
			{
				// Array is const, things in it are not...
				TGeometryParticleHandle<FReal, 3>* Particle0 = const_cast<TGeometryParticleHandle<FReal, 3>*>(ParticlePair[0]);
				TGeometryParticleHandle<FReal, 3>* Particle1 = const_cast<TGeometryParticleHandle<FReal, 3>*>(ParticlePair[1]);

				// Particles may have been disabled or made kinematic
				bool bAnyDisabled = TGenericParticleHandle<FReal, 3>(Particle0)->Disabled() || TGenericParticleHandle<FReal, 3>(Particle1)->Disabled();
				bool bAnyDynamic = TGenericParticleHandle<FReal, 3>(Particle0)->IsDynamic() || TGenericParticleHandle<FReal, 3>(Particle1)->IsDynamic();
				if (bAnyDynamic && !bAnyDisabled)
				{
					const TAABB<FReal, 3>& Box0 = Particle0->WorldSpaceInflatedBounds();
					const TAABB<FReal, 3>& Box1 = Particle1->WorldSpaceInflatedBounds();
					if (Box0.Intersects(Box1))
					{
						NarrowPhase.GenerateCollisions(Dt, Receiver, Particle0, Particle1, BoundsThickness, StatData);
					}
				}
			}
		}

	private:
		const TArray<FParticlePair>& ParticlePairs;
	};
}
