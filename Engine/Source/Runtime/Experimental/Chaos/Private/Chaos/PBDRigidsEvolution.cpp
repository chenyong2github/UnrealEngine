// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolution.h"

#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "Chaos/PBDRigidsEvolutionPGS.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"
#include "ChaosLog.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/DebugDrawQueue.h"

#define LOCTEXT_NAMESPACE "Chaos"

DEFINE_LOG_CATEGORY(LogChaos);

using namespace Chaos;

template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::TPBDRigidsEvolutionBase(TPBDRigidParticles<T, d>&& InParticles, int32 InNumIterations)
    : Particles(MoveTemp(InParticles))
    , Clustering(static_cast<FPBDRigidsEvolution&>(*this), Particles)
	, Time(0)
    , DebugSubstep()
	, NumIterations(InNumIterations)
{
	Particles.AddArray(&Collided);
	Particles.AddArray(&PhysicsMaterials);
	Particles.AddArray(&PerParticlePhysicsMaterials);
	Particles.AddArray(&ParticleDisableCount);
	InitializeFromParticleData(0);
}

#if !UE_BUILD_SHIPPING
template<typename FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
void TPBDRigidsEvolutionBase<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::SerializeForPerfTest(FChaosArchive& Ar)
{
	//todo: this is incomplete, but useful for perf testing
	Ar << Particles;
	Ar << PhysicsMaterials;
	Ar << PerParticlePhysicsMaterials;
	InitializeFromParticleData(0);
}
#endif

template class Chaos::TPBDRigidsEvolutionBase<TPBDRigidsEvolutionGBF<float, 3>, TPBDCollisionConstraint<float, 3>, float, 3>;
template class Chaos::TPBDRigidsEvolutionBase<TPBDRigidsEvolutionPGS<float, 3>, TPBDCollisionConstraintPGS<float, 3>, float, 3>;


#undef LOCTEXT_NAMESPACE