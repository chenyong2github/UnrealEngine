// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/MidPhaseModification.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Collision/ParticlePairMidPhase.h"
#include "Chaos/Particle/ParticleUtilities.h"
#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/ParticleHandleFwd.h"

namespace Chaos
{
	void FMidPhaseModifier::DisableCCD()
	{
		MidPhase->SetCCDIsActive(false);
	}

	void FMidPhaseModifier::GetParticles(
		const FGeometryParticleHandle** Particle0,
		const FGeometryParticleHandle** Particle1) const
	{
		*Particle0 = MidPhase->GetParticle0();
		*Particle1 = MidPhase->GetParticle1();
	}

	const FGeometryParticleHandle* FMidPhaseModifier::GetOtherParticle(const FGeometryParticleHandle* InParticle) const
	{
		if (MidPhase)
		{
			const FGeometryParticleHandle* Particle0 = MidPhase->GetParticle0();
			const FGeometryParticleHandle* Particle1 = MidPhase->GetParticle1();
			if (InParticle == Particle0)
			{
				return Particle1;
			}
			else if (InParticle == Particle1)
			{
				return Particle0;
			}
		}
		return nullptr;
	}

	FMidPhaseModifierParticleIterator FMidPhaseModifierParticleRange::begin() const
	{
		return FMidPhaseModifierParticleIterator(Accessor, Particle);
	}

	FMidPhaseModifierParticleIterator FMidPhaseModifierParticleRange::end() const
	{
		return FMidPhaseModifierParticleIterator(Accessor, Particle, Particle->ParticleCollisions().Num());
	}

	FMidPhaseModifierParticleRange FMidPhaseModifierAccessor::GetMidPhases(FGeometryParticleHandle* Particle)
	{
		return FMidPhaseModifierParticleRange(this, Particle);
	}
}
