// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintPGS.h"
#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/Transform.h"
#include "Chaos/Framework/DebugSubstep.h"
#include "HAL/Event.h"

namespace Chaos
{
class FChaosArchive;

struct CHAOS_API FEvolutionStats
{
	int32 ActiveCollisionPoints;
	int32 ActiveShapes;
	int32 ShapesForAllConstraints;
	int32 CollisionPointsForAllConstraints;

	FEvolutionStats()
	{
		Reset();
	}

	void Reset()
	{
		ActiveCollisionPoints = 0;
		ActiveShapes = 0;
		ShapesForAllConstraints = 0;
		CollisionPointsForAllConstraints = 0;
	}

	FEvolutionStats& operator+=(const FEvolutionStats& Other)
	{
		ActiveCollisionPoints += Other.ActiveCollisionPoints;
		ActiveShapes += Other.ActiveShapes;
		ShapesForAllConstraints += Other.ShapesForAllConstraints;
		CollisionPointsForAllConstraints += Other.CollisionPointsForAllConstraints;
		return *this;
	}
};



template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
class TPBDRigidsEvolutionBase
{
  public:
	typedef typename FPBDCollisionConstraint::FRigidBodyContactConstraint FRigidBodyContactConstraint;
	typedef TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d> FRigidClustering;
	typedef TPBDConstraintGraph<T, d> FConstraintGraph;
	typedef TPBDConstraintGraphRule<T, d> FConstraintRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T, const int32)> FForceRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T, const TArray<int32>&)> FUpdateVelocityRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T)> FUpdatePositionRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T, const T, const int32)> FKinematicUpdateRule;

	// TODO(mlentine): Init particles from some type of input
	CHAOS_API TPBDRigidsEvolutionBase(TPBDRigidParticles<T, d>&& InParticles, int32 NumIterations = 1);
	CHAOS_API virtual ~TPBDRigidsEvolutionBase() {}

	CHAOS_API inline void InitializeFromParticleData(const int32 StartIndex)
	{
		if (!StartIndex)
		{
			ActiveIndices.Reset();
			NonDisabledIndices.Reset();
		}
		for (uint32 i = StartIndex; i < Particles.Size(); ++i)
		{
			if(Particles.Disabled(i))
			{
				continue;
			}

			if(Particles.Sleeping(i))
			{
				NonDisabledIndices.Add(i);
				continue;
			}

			NonDisabledIndices.Add(i);
			ActiveIndices.Add(i);
		}

		if (StartIndex && NonDisabledIndices.Num())
		{
			//Hack: we may have inserted double indices, just sort and remove duplicates. Long term should use set
			NonDisabledIndices.Sort();
			int32 Prev = NonDisabledIndices[NonDisabledIndices.Num() - 1];
			for (int32 Idx = NonDisabledIndices.Num() - 2; Idx >= 0; --Idx)
			{
				if (NonDisabledIndices[Idx] == Prev)
				{
					NonDisabledIndices.RemoveAtSwap(Idx);
				}
				else
				{
					Prev = NonDisabledIndices[Idx];
				}
			}
		}

		Clustering.InitTopLevelClusterParents(StartIndex);
	}

	/**
	 * Enable a particle in the same island and state as another particle
	 */
	CHAOS_API void EnableParticle(const int32 ParticleIndex, const int32 ParentParticleIndex)
	{
		Particles.SetDisabledLowLevel(ParticleIndex, false);
		NonDisabledIndices.Add(ParticleIndex);
		ActiveIndices.Add(ParticleIndex);

		ConstraintGraph.EnableParticle(Particles, ParticleIndex, ParentParticleIndex);
	}

	CHAOS_API void DisableParticle(const int32 ParticleIndex)
	{
		Particles.SetDisabledLowLevel(ParticleIndex, true);
		NonDisabledIndices.Remove(ParticleIndex);
		ActiveIndices.Remove(ParticleIndex);

		ConstraintGraph.DisableParticle(Particles, ParticleIndex);

		RemoveConstraints(TSet<int32>({ ParticleIndex }));
	}

	// @todo(ccaulfield): remove uint version
	CHAOS_API void DisableParticles(const TSet<uint32>& InParticleIndices)
	{
		DisableParticles(reinterpret_cast<const TSet<int32>&>(InParticleIndices));
	}
	CHAOS_API void DisableParticles(const TSet<int32>& InParticleIndices)
	{
		for (int32 ParticleIndex : InParticleIndices)
		{
			Particles.SetDisabledLowLevel(ParticleIndex, true);
			NonDisabledIndices.Remove(ParticleIndex);
			ActiveIndices.Remove(ParticleIndex);
		}

		ConstraintGraph.DisableParticles(Particles, InParticleIndices);

		RemoveConstraints(InParticleIndices);
	}

	CHAOS_API void WakeIsland(const int32 Island)
	{
		ConstraintGraph.WakeIsland(Particles, Island);
		for (int32 Particle : ConstraintGraph.GetIslandParticles(Island))
		{
			ActiveIndices.Add(Particle);
		}
	}

	// @todo(ccaulfield): Remove uint version
	CHAOS_API void WakeIslands(const TSet<uint32>& InIslandIndices)
	{
		WakeIslands(reinterpret_cast<const TSet<int32>&>(InIslandIndices));
	}
	CHAOS_API void WakeIslands(const TSet<int32>& InIslandIndices)
	{
		for (int32 Island : InIslandIndices)
		{
			ConstraintGraph.WakeIsland(Particles, Island);
			for (int32 Particle : ConstraintGraph.GetIslandParticles(Island))
			{
				ActiveIndices.Add(Particle);
			}
		}
	}

	CHAOS_API void ReconcileIslands()
	{
		ConstraintGraph.ReconcileIslands(Particles);
	}

	// @todo(ccaulfield): Remove the uint version
	CHAOS_API void RemoveConstraints(const TSet<uint32>& RemovedParticles)
	{
		RemoveConstraints(reinterpret_cast<const TSet<int32>&>(RemovedParticles));
	}
	CHAOS_API void RemoveConstraints(const TSet<int32>& RemovedParticles)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->RemoveConstraints(RemovedParticles);
		}
	}

	//CHAOS_API void SetKinematicUpdateFunction(TFunction<void(TPBDRigidParticles<T, d>&, const T, const T, const int32)> KinematicUpdate) { MKinematicUpdate = KinematicUpdate; }
	CHAOS_API void SetParticleUpdateVelocityFunction(TFunction<void(TPBDRigidParticles<T, d>&, const T, const TArray<int32>& InActiveIndices)> ParticleUpdate) { ParticleUpdateVelocity = ParticleUpdate; }
	CHAOS_API void SetParticleUpdatePositionFunction(TFunction<void(TPBDRigidParticles<T, d>&, const T)> ParticleUpdate) { ParticleUpdatePosition = ParticleUpdate; }
	CHAOS_API void AddForceFunction(TFunction<void(TPBDRigidParticles<T, d>&, const T, const int32)> ForceFunction) { ForceRules.Add(ForceFunction); }
	CHAOS_API void AddConstraintRule(FConstraintRule* ConstraintRule) 
	{ 
		uint32 ContainerId = (uint32)ConstraintRules.Num();
		ConstraintRules.Add(ConstraintRule); 
		ConstraintRule->BindToGraph(ConstraintGraph, ContainerId);
	}

	// @todo(ccaulfield): Disallow direct write access to containers and provide methods to add/remove enable/disable sleep/wake particles

	/**/
	CHAOS_API TPBDRigidParticles<T, d>& GetParticles() { return Particles; }
	CHAOS_API const TPBDRigidParticles<T, d>& GetParticles() const { return Particles; }

	/**/
	CHAOS_API const TArray<int32>& GetIslandParticles(const int32 Island) const { return ConstraintGraph.GetIslandParticles(Island); }
	CHAOS_API int32 NumIslands() const { return ConstraintGraph.NumIslands(); }

	/**/
	CHAOS_API TSet<int32>& GetActiveIndices() { return ActiveIndices; }
	CHAOS_API const TSet<int32>& GetActiveIndices() const { return ActiveIndices; }

	// @todo(ccaulfield): optimize this (member array with dirty flag) when we have removed public write access to ActiveIndices
	CHAOS_API const TArray<int32> GetActiveIndicesArray() const { return ActiveIndices.Array(); }

	/**/
	CHAOS_API TArray<int32>& GetNonDisabledIndices() { return NonDisabledIndices; }
	CHAOS_API const TArray<int32>& GetNonDisabledIndices() const { return NonDisabledIndices; }

	/**/
	CHAOS_API TSet<TTuple<int32, int32>>& GetDisabledCollisions() { return DisabledCollisions; }
	CHAOS_API const TSet<TTuple<int32, int32>>& GetDisabledCollisions() const { return DisabledCollisions; }

	/**/
	FRigidClustering& GetRigidClustering() { return Clustering; }
	const FRigidClustering& GetRigidClustering() const { return Clustering; }

	/**/
	// @todo(ccaulfield): make sure these are hooked up
	CHAOS_API inline void SetIterations(int32 Iterations) { NumIterations = Iterations; }
	CHAOS_API virtual void SetPushOutIterations(int32 PushOutIterations) {}
	CHAOS_API virtual void SetPushOutPairIterations(int32 PushOutPairIterations) {}

	CHAOS_API TSerializablePtr<TChaosPhysicsMaterial<T>> GetPhysicsMaterial(const int32 Index) const { return PhysicsMaterials[Index]; }
	CHAOS_API void SetPhysicsMaterial(const int32 Index, TSerializablePtr<TChaosPhysicsMaterial<T>> InMaterial)
	{
		check(!PerParticlePhysicsMaterials[Index]);	//shouldn't be setting non unique material if a unique one already exists
		PhysicsMaterials[Index] = InMaterial;
	}

	CHAOS_API const TUniquePtr<TChaosPhysicsMaterial<T>>& GetPerParticlePhysicsMaterial(const int32 Index) const { return PerParticlePhysicsMaterials[Index]; }
	CHAOS_API void SetPerParticlePhysicsMaterial(const int32 Index, TUniquePtr<TChaosPhysicsMaterial<T>>&& PerParticleMaterial)
	{
		PhysicsMaterials[Index] = MakeSerializable(PerParticleMaterial);
		PerParticlePhysicsMaterials[Index] = MoveTemp(PerParticleMaterial);
	}


#if !UE_BUILD_SHIPPING
	CHAOS_API void SerializeForPerfTest(FChaosArchive& Ar);
#endif

	/* Return the instance of the debug substep object that manages the debug pause/progress to step/substep commands for this solver. */
	CHAOS_API inline FDebugSubstep& GetDebugSubstep() const { return DebugSubstep; }

	CHAOS_API const FEvolutionStats& GetEvolutionStats() const { return EvolutionStats; }


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
			ConstraintRule->UpdatePositionBasedState(Particles, NonDisabledIndices, Dt);
		}
	}

	void CreateConstraintGraph()
	{
		ConstraintGraph.InitializeGraph(Particles, NonDisabledIndices);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}
	}

	void CreateIslands()
	{
		ConstraintGraph.UpdateIslands(Particles, NonDisabledIndices, ActiveIndices);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}

	void ApplyConstraints(const T Dt, int32 Island)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdateAccelerationStructures(Particles, NonDisabledIndices, Island);
		}

		for (int i = 0; i < NumIterations; ++i)
		{
			for (FConstraintRule* ConstraintRule : ConstraintRules)
			{
				ConstraintRule->ApplyConstraints(Particles, Dt, Island);
			}
		}
	}

	void UpdateVelocities(const T Dt, int32 Island)
	{
		ParticleUpdateVelocity(Particles, Dt, ConstraintGraph.GetIslandParticles(Island));
	}

	void ApplyPushOut(const T Dt, int32 Island)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->ApplyPushOut(Particles, Dt, Island);
		}
	}

	void InitializeAccelerationStructures()
	{
		const TArray<int32>& ActiveIndicesArray = GetActiveIndicesArray();

		ConstraintGraph.InitializeGraph(Particles, ActiveIndicesArray);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}

		ConstraintGraph.ResetIslands(Particles, ActiveIndicesArray);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}

	void UpdateAccelerationStructures(int32 Island)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdateAccelerationStructures(Particles, NonDisabledIndices, Island);
		}
	}

protected:

	TPBDRigidParticles<T, d> Particles;
	FRigidClustering Clustering;
	FConstraintGraph ConstraintGraph;

	TSet<int32> ActiveIndices;
	TArray<int32> NonDisabledIndices;
	TSet<TTuple<int32, int32>> DisabledCollisions;
	T Time;
	mutable FDebugSubstep DebugSubstep;

	// User query data
	TArrayCollectionArray<bool> Collided;
	TArray<FForceRule> ForceRules;
	TArray<FConstraintRule*> ConstraintRules;
	FUpdateVelocityRule ParticleUpdateVelocity;
	FUpdatePositionRule ParticleUpdatePosition;
	FKinematicUpdateRule KinematicUpdate;

	int32 NumIterations;

	TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>> PhysicsMaterials;
	TArrayCollectionArray<TUniquePtr<TChaosPhysicsMaterial<T>>> PerParticlePhysicsMaterials;
	TArrayCollectionArray<int32> ParticleDisableCount;
	FEvolutionStats EvolutionStats;
};

template<class T, int d>
class TPBDRigidsEvolutionPGS : public TPBDRigidsEvolutionBase<TPBDRigidsEvolutionPGS<T, d>, TPBDCollisionConstraintPGS<T, d>, T, d>
{
	typedef TPBDRigidsEvolutionBase<TPBDRigidsEvolutionPGS<T, d>, TPBDCollisionConstraintPGS<T, d>, T, d> Base;
public:
	typedef typename Base::FRigidBodyContactConstraint FRigidBodyContactConstraint;
	typedef typename Base::FRigidClustering FRigidClustering;
	typedef typename Base::FConstraintGraph FConstraintGraph;
	typedef typename Base::FConstraintRule FConstraintRule;
	typedef typename Base::FForceRule FForceRule;
	typedef typename Base::FUpdateVelocityRule FUpdateVelocityRule;
	typedef typename Base::FUpdatePositionRule FUpdatePositionRule;
	typedef typename Base::FKinematicUpdateRule FKinematicUpdateRule;
	typedef TPBDCollisionConstraintPGS<T, d> FCollisionConstraints;
	typedef TPBDConstraintIslandRule<FCollisionConstraints, T, d> FCollisionConstraintRule;

	using Base::AddConstraintRule;
	using Base::ApplyConstraints;
	using Base::ApplyPushOut;
	using Base::CreateConstraintGraph;
	using Base::CreateIslands;
	using Base::GetActiveIndices;
	using Base::GetActiveIndicesArray;
	using Base::GetDebugSubstep;
	using Base::GetIslandParticles;
	using Base::GetNonDisabledIndices;
	using Base::SetParticleUpdatePositionFunction;
	using Base::SetParticleUpdateVelocityFunction;
	using Base::UpdateAccelerationStructures;
	using Base::UpdateConstraintPositionBasedState;
	using Base::UpdateVelocities;


	// TODO(mlentine): Init particles from some type of input
	CHAOS_API TPBDRigidsEvolutionPGS(TPBDRigidParticles<T, d>&& InParticles, int32 NumIterations = 1);
	CHAOS_API ~TPBDRigidsEvolutionPGS() {}

	CHAOS_API void IntegrateV(const TArray<int32>& InActiveIndices, const T dt);
	CHAOS_API void IntegrateX(const TArray<int32>& InActiveIndices, const T dt);
	CHAOS_API void AdvanceOneTimeStep(const T dt);

private:
	using Base::ActiveIndices;
	using Base::Clustering;
	using Base::ConstraintRules;
	using Base::ConstraintGraph;
	using Base::Collided;
	using Base::EvolutionStats;
	using Base::ForceRules;
	using Base::KinematicUpdate;
	using Base::NumIterations;
	using Base::Particles;
	using Base::ParticleDisableCount;
	using Base::ParticleUpdatePosition;
	using Base::ParticleUpdateVelocity;
	using Base::Time;
	using Base::NonDisabledIndices;
	using Base::PhysicsMaterials;
	using Base::PerParticlePhysicsMaterials;

	FCollisionConstraints CollisionConstraints;
	FCollisionConstraintRule CollisionRule;
};

template<class T, int d>
class TPBDRigidsEvolutionGBF : public TPBDRigidsEvolutionBase<TPBDRigidsEvolutionGBF<T, d>, TPBDCollisionConstraint<T, d>, T, d>
{
	typedef TPBDRigidsEvolutionBase<TPBDRigidsEvolutionGBF<T, d>, TPBDCollisionConstraint<T, d>, T, d> Base;
public:
	typedef typename Base::FRigidBodyContactConstraint FRigidBodyContactConstraint;
	typedef typename Base::FRigidClustering FRigidClustering;
	typedef typename Base::FConstraintGraph FConstraintGraph;
	typedef typename Base::FConstraintRule FConstraintRule;
	typedef typename Base::FForceRule FForceRule;
	typedef typename Base::FUpdateVelocityRule FUpdateVelocityRule;
	typedef typename Base::FUpdatePositionRule FUpdatePositionRule;
	typedef typename Base::FKinematicUpdateRule FKinematicUpdateRule;
	typedef TPBDCollisionConstraint<T, d> FCollisionConstraints;
	typedef TPBDConstraintColorRule<FCollisionConstraints, T, d> FCollisionConstraintRule;

	static const int32 DefaultNumIterations = 1;
	static const int32 DefaultNumPushOutIterations = 5;
	static const int32 DefaultNumPushOutPairIterations = 2;

	using Base::AddConstraintRule;
	using Base::ApplyConstraints;
	using Base::ApplyPushOut;
	using Base::CreateConstraintGraph;
	using Base::CreateIslands;
	using Base::GetActiveIndices;
	using Base::GetActiveIndicesArray;
	using Base::GetDebugSubstep;
	using Base::GetIslandParticles;
	using Base::GetNonDisabledIndices;
	using Base::SetParticleUpdatePositionFunction;
	using Base::SetParticleUpdateVelocityFunction;
	using Base::UpdateAccelerationStructures;
	using Base::UpdateConstraintPositionBasedState;
	using Base::UpdateVelocities;

	// TODO(mlentine): Init particles from some type of input
	CHAOS_API TPBDRigidsEvolutionGBF(TPBDRigidParticles<T, d>&& InParticles, int32 NumIterations = 1);
	CHAOS_API ~TPBDRigidsEvolutionGBF() {}

	FCollisionConstraints& GetCollisionConstraints() { return CollisionConstraints; }
	const FCollisionConstraints& GetCollisionConstraints() const { return CollisionConstraints; }

	CHAOS_API void AdvanceOneTimeStep(const T dt);

	CHAOS_API void Integrate(const TArray<int32>& InActiveIndices, const T dt);

	CHAOS_API const ISpatialAcceleration<T, d>& GetSpatialAcceleration() const { return GetCollisionConstraints().GetSpatialAcceleration(); }
	CHAOS_API void ReleaseSpatialAcceleration() const { GetCollisionConstraints().ReleaseSpatialAcceleration(); }

	CHAOS_API virtual void SetPushOutIterations(int32 PushOutIterations) override { CollisionRule.SetPushOutIterations(PushOutIterations); }
	CHAOS_API virtual void SetPushOutPairIterations(int32 PushOutPairIterations) override { CollisionConstraints.SetPushOutPairIterations(PushOutPairIterations); }

	void GatherStats();
	void DebugDraw();

private:
	using Base::ActiveIndices;
	using Base::Clustering;
	using Base::ConstraintRules;
	using Base::ConstraintGraph;
	using Base::Collided;
	using Base::EvolutionStats;
	using Base::ForceRules;
	using Base::KinematicUpdate;
	using Base::NumIterations;
	using Base::Particles;
	using Base::ParticleDisableCount;
	using Base::ParticleUpdatePosition;
	using Base::ParticleUpdateVelocity;
	using Base::Time;
	using Base::NonDisabledIndices;
	using Base::PhysicsMaterials;
	using Base::PerParticlePhysicsMaterials;


	FCollisionConstraints CollisionConstraints;
	FCollisionConstraintRule CollisionRule;
};
}
