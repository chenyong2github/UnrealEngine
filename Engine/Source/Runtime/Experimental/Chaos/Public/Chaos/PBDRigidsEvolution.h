// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionConstraintPGS.h"
#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Transform.h"
#include "Chaos/Framework/DebugSubstep.h"
#include "HAL/Event.h"
#include "Chaos/PBDRigidsSOAs.h"

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
	typedef TFunction<void(TTransientPBDRigidParticleHandle<T,d>& Particle, const T)> FForceRule;
	typedef TFunction<void(const TParticleView<TPBDRigidParticles<T, d>>&, const T)> FUpdateVelocityRule;
	typedef TFunction<void(const TParticleView<TPBDRigidParticles<T, d>>&, const T)> FUpdatePositionRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T, const T, const int32)> FKinematicUpdateRule;

	CHAOS_API TPBDRigidsEvolutionBase(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations = 1);
	CHAOS_API virtual ~TPBDRigidsEvolutionBase()
	{
		Particles.GetParticleHandles().RemoveArray(&PhysicsMaterials);
		Particles.GetParticleHandles().RemoveArray(&PerParticlePhysicsMaterials);
		Particles.GetParticleHandles().RemoveArray(&ParticleDisableCount);
		Particles.GetParticleHandles().RemoveArray(&Collided);
	}

	CHAOS_API TArray<TGeometryParticleHandle<T, d>*> CreateStaticParticles(int32 NumParticles, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>()) { return Particles.CreateStaticParticles(NumParticles, Params); }
	CHAOS_API TArray<TKinematicGeometryParticleHandle<T, d>*> CreateKinematicParticles(int32 NumParticles, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>()) { return Particles.CreateKinematicParticles(NumParticles, Params); }
	CHAOS_API TArray<TPBDRigidParticleHandle<T, d>*> CreateDynamicParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>()) { return Particles.CreateDynamicParticles(NumParticles, Params); }
	CHAOS_API TArray<TPBDRigidClusteredParticleHandle<T, d>*> CreateClusteredParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>()) { return Particles.CreateClusteredParticles(NumParticles, Params); }

	CHAOS_API void AddForceFunction(FForceRule ForceFunction) { ForceRules.Add(ForceFunction); }
	CHAOS_API void SetParticleUpdateVelocityFunction(FUpdateVelocityRule ParticleUpdate) { ParticleUpdateVelocity = ParticleUpdate; }
	CHAOS_API void SetParticleUpdatePositionFunction(FUpdatePositionRule ParticleUpdate) { ParticleUpdatePosition = ParticleUpdate; }

	CHAOS_API TGeometryParticleHandles<T, d>& GetParticleHandles() { return Particles.GetParticleHandles(); }
	CHAOS_API const TGeometryParticleHandles<T, d>& GetParticleHandles() const { return Particles.GetParticleHandles(); }

	CHAOS_API TPBDRigidsSOAs<T,d>& GetParticles() { return Particles; }
	CHAOS_API const TPBDRigidsSOAs<T, d>& GetParticles() const { return Particles; }

	typedef TPBDConstraintGraph<T, d> FConstraintGraph;
	typedef TPBDConstraintGraphRule<T, d> FConstraintRule;

	CHAOS_API void AddConstraintRule(FConstraintRule* ConstraintRule)
	{
		uint32 ContainerId = (uint32)ConstraintRules.Num();
		ConstraintRules.Add(ConstraintRule);
		ConstraintRule->BindToGraph(ConstraintGraph, ContainerId);
	}

	CHAOS_API void EnableParticle(TGeometryParticleHandle<T,d>* Particle, const TGeometryParticleHandle<T, d>* ParentParticle)
	{
		Particles.EnableParticle(Particle);
		ConstraintGraph.EnableParticle(Particle, ParentParticle);
	}

	CHAOS_API void DisableParticle(TGeometryParticleHandle<T,d>* Particle)
	{
		Particles.DisableParticle(Particle);
		ConstraintGraph.DisableParticle(Particle);

		RemoveConstraints(TSet<TGeometryParticleHandle<T,d>*>({ Particle }));
	}

	CHAOS_API void DestroyParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		ConstraintGraph.RemoveParticle(Particle);
		RemoveConstraints(TSet<TGeometryParticleHandle<T, d>*>({ Particle }));
		Particles.DestroyParticle(Particle);
	}

	CHAOS_API void DisableParticles(const TSet<TGeometryParticleHandle<T,d>*>& InParticles)
	{
		for (TGeometryParticleHandle<T, d>* Particle : InParticles)
		{
			Particles.DisableParticle(Particle);
		}

		ConstraintGraph.DisableParticles(InParticles);

		RemoveConstraints(InParticles);
	}

	CHAOS_API void WakeIsland(const int32 Island)
	{
		ConstraintGraph.WakeIsland(Island);
		//Update Particles SOAs
		/*for (auto Particle : ContactGraph.GetIslandParticles(Island))
		{
			ActiveIndices.Add(Particle);
		}*/
	}

	// @todo(ccaulfield): Remove the uint version
	CHAOS_API void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->RemoveConstraints(RemovedParticles);
		}
	}

	//TEMP: this is only needed while clustering continues to use indices directly
	const auto& GetActiveClusteredArray() const { return Particles.GetActiveClusteredArray(); }
	const auto& GetNonDisabledClusteredArray() const { return Particles.GetNonDisabledClusteredArray(); }

	CHAOS_API TSerializablePtr<TChaosPhysicsMaterial<T>> GetPhysicsMaterial(const TGeometryParticleHandle<T, d>* Particle) const { return Particle->AuxilaryValue(PhysicsMaterials); }
	CHAOS_API void SetPhysicsMaterial(TGeometryParticleHandle<T,d>* Particle, TSerializablePtr<TChaosPhysicsMaterial<T>> InMaterial)
	{
		check(!Particle->AuxilaryValue(PerParticlePhysicsMaterials)); //shouldn't be setting non unique material if a unique one already exists
		Particle->AuxilaryValue(PhysicsMaterials) = InMaterial;
	}

	CHAOS_API const TArray<TGeometryParticleHandle<T,d>*>& GetIslandParticles(const int32 Island) const { return ConstraintGraph.GetIslandParticles(Island); }
	CHAOS_API int32 NumIslands() const { return ConstraintGraph.NumIslands(); }

	void InitializeAccelerationStructures()
	{
		ConstraintGraph.InitializeGraph(Particles.GetNonDisabledView());

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}

		ConstraintGraph.ResetIslands(Particles.GetNonDisabledDynamicView());

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

	const auto& GetRigidClustering() const { return Clustering; }
	auto& GetRigidClustering() { return Clustering; }

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
		ConstraintGraph.InitializeGraph(Particles.GetNonDisabledView());

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}
	}

	void CreateIslands()
	{
		ConstraintGraph.UpdateIslands(Particles.GetNonDisabledDynamicView(), Particles);

		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}
	
	void UpdateVelocities(const T Dt, int32 Island)
	{
		ParticleUpdateVelocity(Particles.GetActiveParticlesView(), Dt);
	}

	void ApplyPushOut(const T Dt, int32 Island)
	{
		for (FConstraintRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->ApplyPushOut(Dt, Island);
		}
	}


	TArray<FForceRule> ForceRules;
	FUpdateVelocityRule ParticleUpdateVelocity;
	FUpdatePositionRule ParticleUpdatePosition;
	FKinematicUpdateRule KinematicUpdate;
	TArray<FConstraintRule*> ConstraintRules;
	FConstraintGraph ConstraintGraph;
	TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>> PhysicsMaterials;
	TArrayCollectionArray<TUniquePtr<TChaosPhysicsMaterial<T>>> PerParticlePhysicsMaterials;
	TArrayCollectionArray<int32> ParticleDisableCount;
	TArrayCollectionArray<bool> Collided;

	TPBDRigidsSOAs<T, d>& Particles;

	TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d> Clustering;

	int32 NumIterations;
};

}
