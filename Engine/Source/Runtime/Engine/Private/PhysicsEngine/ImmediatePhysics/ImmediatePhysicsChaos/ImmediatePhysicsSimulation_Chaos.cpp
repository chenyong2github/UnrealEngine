// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsSimulation_Chaos.h"

#if INCLUDE_CHAOS

#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsActorHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsChaos/ImmediatePhysicsJointHandle_Chaos.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsStats.h"

#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

namespace ImmediatePhysics_Chaos
{

	//
	//
	//

	FSimulation::FSimulation()
		: NumActiveActorHandles(0)
		, Gravity(FVector::ZeroVector)
	{
		using namespace Chaos;

		const int DefaultSolverIterations = 5;
		Particles = MakeUnique<TPBDRigidsSOAs<FReal, Dimensions>>();
		Joints = MakeUnique<TPBDJointConstraints<FReal, Dimensions>>();
		JointsRule = MakeUnique<TPBDConstraintIslandRule<TPBDJointConstraints<FReal, Dimensions>, FReal, Dimensions>>(*Joints);
		Evolution = MakeUnique<TPBDRigidsEvolutionGBF<FReal, Dimensions>>(*Particles.Get(), DefaultSolverIterations);

		Evolution->AddConstraintRule(JointsRule.Get());

		Evolution->AddForceFunction([this](TTransientPBDRigidParticleHandle<FReal, Dimensions>& Particle, const FReal Dt)
			{
				Particle.F() += Gravity * Particle.M();
			});
	}

	FSimulation::~FSimulation()
	{
		for (FActorHandle* ActorHandle : ActorHandles)
		{
			delete ActorHandle;
		}
		ActorHandles.Empty();

		for (FJointHandle* JointHandle : JointHandles)
		{
			delete JointHandle;
		}
		JointHandles.Empty();

		Evolution.Reset();
		JointsRule.Reset();
		Joints.Reset();
		Particles.Reset();
	}

	FActorHandle* FSimulation::CreateStaticActor(FBodyInstance* BodyInstance)
	{
		return CreateActor(EActorType::StaticActor, BodyInstance, BodyInstance->GetUnrealWorldTransform());
	}

	FActorHandle* FSimulation::CreateKinematicActor(FBodyInstance* BodyInstance, const FTransform& Transform)
	{
		return CreateActor(EActorType::KinematicActor, BodyInstance, Transform);
	}

	FActorHandle* FSimulation::CreateDynamicActor(FBodyInstance* BodyInstance, const FTransform& Transform)
	{
		return CreateActor(EActorType::DynamicActor, BodyInstance, Transform);
	}

	FActorHandle* FSimulation::CreateActor(EActorType ActorType, FBodyInstance* BodyInstance, const FTransform& Transform)
	{
		FActorHandle* ActorHandle = new FActorHandle(Evolution.Get(), ActorType, BodyInstance, Transform);
		int ActorIndex = ActorHandles.Add(ActorHandle);
		return ActorHandle;
	}

	void FSimulation::DestroyActor(FActorHandle* ActorHandle)
	{
		// @todo(ccaulfield): FActorHandle could remember its index to optimize this
		ActorHandles.Remove(ActorHandle);
		delete ActorHandle;
	}

	FJointHandle* FSimulation::CreateJoint(FConstraintInstance* ConstraintInstance, FActorHandle* Body1, FActorHandle* Body2)
	{
		FJointHandle* JointHandle = new FJointHandle(Joints.Get(), ConstraintInstance, Body1, Body2);
		JointHandles.Add(JointHandle);
		return JointHandle;
	}

	void FSimulation::DestroyJoint(FJointHandle* JointHandle)
	{
		// @todo(ccaulfield): FJointHandle could remember its index to optimize this
		JointHandles.Remove(JointHandle);
		delete JointHandle;
	}

	void FSimulation::SetNumActiveBodies(int32 InNumActiveActorHandles)
	{
		if (InNumActiveActorHandles < NumActiveActorHandles)
		{
			for (int ActorHandleIndex = InNumActiveActorHandles; ActorHandleIndex < NumActiveActorHandles; ++ActorHandleIndex)
			{
				GetActorHandle(ActorHandleIndex)->SetEnabled(false);
			}
		}
		else if (InNumActiveActorHandles > NumActiveActorHandles)
		{
			check(InNumActiveActorHandles <= ActorHandles.Num());
			for (int ActorHandleIndex = NumActiveActorHandles; ActorHandleIndex < InNumActiveActorHandles; ++ActorHandleIndex)
			{
				GetActorHandle(ActorHandleIndex)->SetEnabled(true);
			}
		}
	
		NumActiveActorHandles = InNumActiveActorHandles;
	}

	void FSimulation::SetIgnoreCollisionPairTable(const TArray<FIgnorePair>& InIgnoreCollisionPairTable)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
		IgnoreCollisionPairTable.Reset();
		for (const FIgnorePair& IgnorePair : InIgnoreTable)
		{
			TSet<FActorHandle*>& IgnoreActorsA = IgnoreCollisionPairTable.FindOrAdd(IgnorePair.A);
			IgnoreActorsA.Add(IgnorePair.B);

			TSet<FActorHandle*>& IgnoreActorsB = IgnoreCollisionPairTable.FindOrAdd(IgnorePair.B);
			IgnoreActorsB.Add(IgnorePair.A);
		}
		bRecreateIterationCache = true;
#endif
	}

	void FSimulation::SetIgnoreCollisionActors(const TArray<FActorHandle*>& InIgnoreCollisionActors)
	{
#if IMMEDIATEPHYSICS_CHAOS_TODO
		IgnoreCollisionActors.Reset();
		IgnoreCollisionActors.Append(InIgnoreCollisionActors);
		bRecreateIterationCache = true;
#endif
	}

	DECLARE_CYCLE_STAT(TEXT("FSimulation::Simulate_Chaos"), STAT_ImmediateSimulate_Chaos, STATGROUP_ImmediatePhysics);

	void FSimulation::Simulate(float DeltaTime, const FVector& InGravity)
	{
		SCOPE_CYCLE_COUNTER(STAT_ImmediateSimulate_Chaos);

		Gravity = InGravity;

		Evolution->AdvanceOneTimeStep(DeltaTime);
	}
}

#endif // INCLUDE_CHAOS