// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolution2.h"
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
#include "Chaos/PBDRigidsEvolutionGBF2.h"

namespace Chaos
{
	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TPBDRigidsEvolutionBase2<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::TPBDRigidsEvolutionBase2(int32 InNumIterations)
		: NumIterations(InNumIterations)
	{
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	template <typename TParticleHandleType, typename TParticles>
	TArray<TParticleHandleType*> TPBDRigidsEvolutionBase2<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::CreateParticlesHelper(int32 NumParticles, TParticles& Particles, const TGeometryParticleParameters<T,d>& Params)
	{
		const int32 ParticlesStartIdx = Particles.Size();
		Particles.AddParticles(NumParticles);
		TArray<TParticleHandleType*> ReturnHandles;
		ReturnHandles.AddUninitialized(NumParticles);

		const int32 HandlesStartIdx = ParticleHandles.Size();
		ParticleHandles.AddHandles(NumParticles);
		if (Params.bDisabled == false)
		{
			NonDisabledHandles.Reserve(NonDisabledHandles.Num() + NumParticles);
		}

		bool bActive = false;
		if (TParticleHandleType::StaticType() == EParticleType::Dynamic)
		{
			bActive = !static_cast<const TPBDRigidParticleParameters<T, d>&>(Params).bStartSleeping;
		}

		if (bActive)
		{
			ActiveParticles.Reserve(ActiveParticles.Num() + NumParticles);
		}
		
		for (int32 Count = 0; Count < NumParticles; ++Count)
		{
			const int32 ParticleIdx = Count + ParticlesStartIdx;
			const int32 HandleIdx = Count + HandlesStartIdx;

			TParticleHandleType* NewParticleHandle = new TParticleHandleType(&Particles, ParticleIdx, HandleIdx);
			ParticleHandles.Handle(HandleIdx).Reset(NewParticleHandle);
			ReturnHandles[Count] = NewParticleHandle;

			if (Params.bDisabled == false)
			{
				NonDisabledHandles.Add(NewParticleHandle);
			}

			if (bActive)
			{
				ActiveParticles.Add(NewParticleHandle);
			}
		}

		return ReturnHandles;
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TArray<TGeometryParticleHandle<T,d>*> TPBDRigidsEvolutionBase2<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::CreateStaticParticles(int32 NumParticles, const TGeometryParticleParameters<T, d>& Params)
	{
		TGeometryParticles<T, d>& ChosenStaticParticles = Params.bDisabled ? StaticDisabledParticles : StaticParticles;
		return CreateParticlesHelper<TGeometryParticleHandle<T, d>>(NumParticles, ChosenStaticParticles, Params);
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TArray<TKinematicGeometryParticleHandle<T, d>*> TPBDRigidsEvolutionBase2<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::CreateKinematicParticles(int32 NumKinematics, const TKinematicGeometryParticleParameters<T, d>& KinematicParams)
	{
		TKinematicGeometryParticles<T, d>& ChosenKinematicParticles = KinematicParams.bDisabled ? KinematicDisabledParticles : KinematicParticles;
		return CreateParticlesHelper<TKinematicGeometryParticleHandle<T,d>>(NumKinematics, ChosenKinematicParticles, KinematicParams);
	}

	template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
	TArray<TPBDRigidParticleHandle<T, d>*> TPBDRigidsEvolutionBase2<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d>::CreateDynamicParticles(int32 NumDynamics, const TPBDRigidParticleParameters<T, d>& DynamicParams)
	{
		TPBDRigidParticles<T, d>* ChosenDynamicParticles;
		if (DynamicParams.bDisabled)
		{
			ChosenDynamicParticles = &DynamicDisabledParticles;
		}
		else
		{
			ChosenDynamicParticles = DynamicParams.bStartSleeping ? &DynamicAsleepParticles : &DynamicAwakeParticles;
		}

		return CreateParticlesHelper<TPBDRigidParticleHandle<T, d>>(NumDynamics, *ChosenDynamicParticles, DynamicParams);
	}
}

template class Chaos::TPBDRigidsEvolutionBase2<Chaos::TPBDRigidsEvolutionGBF2<float, 3>, Chaos::FConstraintHack, float, 3>;