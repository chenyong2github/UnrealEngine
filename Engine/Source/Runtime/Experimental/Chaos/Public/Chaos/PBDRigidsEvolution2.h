// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintPGS.h"
#include "Chaos/PBDConstraintGraph2.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDConstraintRule2.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Transform.h"
#include "Chaos/Framework/DebugSubstep.h"
#include "HAL/Event.h"
#include "Chaos/ParticleHandle.h"

namespace Chaos
{
class FChaosArchive;

template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
class TPBDRigidsEvolutionBase2
{
  public:
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T, const int32)> FForceRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T)> FUpdateVelocityRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T)> FUpdatePositionRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T, const T, const int32)> FKinematicUpdateRule;

	CHAOS_API TPBDRigidsEvolutionBase2(int32 InNumIterations = 1);
	CHAOS_API virtual ~TPBDRigidsEvolutionBase2() {}

	CHAOS_API TArray<TGeometryParticleHandle<T, d>*> CreateStaticParticles(int32 NumParticles, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T,d>());
	CHAOS_API TArray<TKinematicGeometryParticleHandle<T, d>*> CreateKinematicParticles(int32 NumParticles, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>());
	CHAOS_API TArray<TPBDRigidParticleHandle<T, d>*> CreateDynamicParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>());

	CHAOS_API void AddForceFunction(TFunction<void(TPBDRigidParticles<T, d>&, const T, const int32)> ForceFunction) { ForceRules.Add(ForceFunction); }
	CHAOS_API void SetParticleUpdateVelocityFunction(TFunction<void(TPBDRigidParticles<T, d>&, const T)> ParticleUpdate) { ParticleUpdateVelocity = ParticleUpdate; }
	CHAOS_API void SetParticleUpdatePositionFunction(TFunction<void(TPBDRigidParticles<T, d>&, const T)> ParticleUpdate) { ParticleUpdatePosition = ParticleUpdate; }

	CHAOS_API TGeometryParticleHandles<T, d>& GetParticleHandles() { return ParticleHandles; }
	CHAOS_API const TGeometryParticleHandles<T, d>& GetParticleHandles() const { return ParticleHandles; }

	typedef TPBDConstraintGraph2<T, d> FConstraintGraph;
	typedef TPBDConstraintGraphRule2<T, d> FConstraintRule;

	CHAOS_API void AddConstraintRule(FConstraintRule* ConstraintRule)
	{
		uint32 ContainerId = (uint32)ConstraintRules.Num();
		ConstraintRules.Add(ConstraintRule);
		ConstraintRule->BindToGraph(ConstraintGraph, ContainerId);
	}

protected:
	int32 NumConstraints() const
	{
		int32 NumConstraints = 0;
		for (const FConstraintRule* ConstraintRule : ConstraintRules)
		{
			NumConstraints += ConstraintRule->NumConstraints();
		}
		return NumConstraints;
	}

	void UpdateConstraintPositionBasedState(T Dt)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdatePositionBasedState(Dt);
		}
	}

	void CreateConstraintGraph()
	{
		ConstraintGraph.InitializeGraph(NonDisabledHandles);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}
	}

	void CreateIslands()
	{
		ConstraintGraph.UpdateIslands(NonDisabledHandles, ActiveParticles);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}

	void ApplyConstraints(const T Dt, int32 Island)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdateAccelerationStructures(Island);
		}

		for (int i = 0; i < NumIterations; ++i)
		{
			for (FConstraintRule* ConstraintRule : ConstraintRules)
			{
				ConstraintRule->ApplyConstraints(Dt, Island);
			}
		}
	}

	void UpdateVelocities(const T Dt, int32 Island)
	{
		ParticleUpdateVelocity(DynamicAwakeParticles, Dt);
	}

	void ApplyPushOut(const T Dt, int32 Island)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->ApplyPushOut(Dt, Island);
		}
	}

	void InitializeAccelerationStructures()
	{
		ConstraintGraph.InitializeGraph(NonDisabledHandles);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}

		ConstraintGraph.ResetIslands(NonDisabledHandles);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}

	void UpdateAccelerationStructures(int32 Island)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdateAccelerationStructures(Island);
		}
	}

	template <typename TParticleHandleType, typename TParticles>
	TArray<TParticleHandleType*> CreateParticlesHelper(int32 NumParticles, TParticles& Particles, const TGeometryParticleParameters<T, d>& Params);

	TArray<FForceRule> ForceRules;
	FUpdateVelocityRule ParticleUpdateVelocity;
	FUpdatePositionRule ParticleUpdatePosition;
	FKinematicUpdateRule KinematicUpdate;
	TArray<FConstraintRule*> ConstraintRules;
	FConstraintGraph ConstraintGraph;

	TGeometryParticles<T, d> StaticParticles;
	TGeometryParticles<T, d> StaticDisabledParticles;

	TKinematicGeometryParticles<T, d> KinematicParticles;
	TKinematicGeometryParticles<T, d> KinematicDisabledParticles;

	TPBDRigidParticles<T, d> DynamicAsleepParticles;
	TPBDRigidParticles<T, d> DynamicAwakeParticles;
	TPBDRigidParticles<T, d> DynamicDisabledParticles;

	TGeometryParticleHandles<T, d> ParticleHandles;
	TArray<TGeometryParticleHandle<T, d>*> NonDisabledHandles;
	TSet<TGeometryParticleHandle<T, d>*> ActiveParticles;

	int32 NumIterations;
};

}
