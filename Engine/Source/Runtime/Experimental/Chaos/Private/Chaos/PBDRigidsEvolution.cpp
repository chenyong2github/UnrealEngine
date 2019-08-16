// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Levelset.h"
#include "Chaos/ChaosPerfTest.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"

namespace Chaos
{
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::TPBDRigidsEvolutionBase(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations)
		: Particles(InParticles)
		, Clustering(static_cast<FPBDRigidsEvolution&>(*this), Particles.GetClusteredParticles())
		, NumIterations(InNumIterations)
	{
		Particles.GetParticleHandles().AddArray(&PhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&PerParticlePhysicsMaterials);
		Particles.GetParticleHandles().AddArray(&ParticleDisableCount);
		Particles.GetParticleHandles().AddArray(&Collided);
	}
}

template class Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraint<float,3>, float, 3>;