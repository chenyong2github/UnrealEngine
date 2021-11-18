// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Collision/ParticleCollisions.h"
#include "Chaos/Collision/ParticlePairMidPhase.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDCollisionConstraints.h"

namespace Chaos
{
	FParticleCollisions::FParticleCollisions()
	{
	}

	FParticleCollisions::~FParticleCollisions()
	{
	}

	void FParticleCollisions::Reset()
	{
		ParticlePairs.Reset();
	}

	void FParticleCollisions::AddParticlePair(FParticlePairMidPhase* MidPhase)
	{
		ParticlePairs.Add(MidPhase);
	}

	void FParticleCollisions::RemoveParticlePair(FParticlePairMidPhase* MidPhase)
	{
		// @todo(chaos): store the index on the midphase (need one index for each particle)
		ParticlePairs.Remove(MidPhase);
	}

	void FParticleCollisions::VisitCollisions(const FPBDCollisionVisitor& Visitor) const
	{
		for (FParticlePairMidPhase* MidPhase : ParticlePairs)
		{
			MidPhase->VisitCollisions(Visitor);
		}
	}

}