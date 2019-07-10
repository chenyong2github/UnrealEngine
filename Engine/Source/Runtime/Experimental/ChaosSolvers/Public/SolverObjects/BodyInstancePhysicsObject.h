// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "SolverObject.h"
#include "Chaos/PBDPositionConstraints.h"
#include "PBDRigidsSolver.h"
#include "PhysicsInterfaceTypesCore.h"

class FInitialState
{
public:
	FInitialState()
		: Mass(0.f)
		, InvMass(0.f)
		, InertiaTensor(1.f) 
	{}

	FInitialState(float MassIn, float InvMassIn, FVector InertiaTensorIn)
		: Mass(MassIn)
		, InvMass(InvMassIn)
		, InertiaTensor(InertiaTensorIn)
	{}


	float GetMass() const { return Mass; }
	float GetInverseMass() const { return InvMass; }
	FVector GetInertiaTensor() const { return InertiaTensor; }

private:
	float Mass;
	float InvMass;
	FVector InertiaTensor;
};

class CHAOSSOLVERS_API FBodyInstancePhysicsObject : public TSolverObject<FBodyInstancePhysicsObject>
{
public:
	using FCallbackInitFunc = TFunction<void(FActorCreationParams&, FParticlesType&, TArray<int32>&)>;

	FBodyInstancePhysicsObject() = delete;
	FBodyInstancePhysicsObject(UObject* InOwner, FInitialState InitialState);
	virtual ~FBodyInstancePhysicsObject();

	// Scene API
	void Initialize() {}

	// Callbacks
	bool IsSimulating() const { return true; }
	void FieldParameterUpdateCallback(Chaos::FPBDRigidsSolver* InSolver, FParticlesType& InParticles, Chaos::TArrayCollectionArray<float>& Strains, Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, TMap<int32, int32>& PositionTargetedParticles, const TArray<FKinematicProxy>& AnimatedPositions, const float InTime) {}
	void FieldForcesUpdateCallback(Chaos::FPBDRigidsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector> & Force, Chaos::TArrayCollectionArray<FVector> & Torque, const float Time) {}
	void EndFrameCallback(const float InDt) {}
	void BufferCommand(Chaos::FPBDRigidsSolver* InSolver, const FFieldSystemCommand& InCommand) {}

	// Inactive Callbacks
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime) {}
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) {}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<SolverObjectWrapper> & SolverObjectReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap) {}
	void StartFrameCallback(const float InDt, const float InTime) {}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles);
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) {}

	void SyncBeforeDestroy() {}
	void OnRemoveFromScene();
	void CacheResults() {}
	void FlipCache() {}
	void SyncToCache() {}

	// Index in Particles Array
	int32 RigidBodyId;
	int32 GetRigidBodyId() { return RigidBodyId; }
	void SetRigidBodyId(int InRigidBodyId) { RigidBodyId = InRigidBodyId; }

	// Engine Interface Functions
	FActorCreationParams CreationParameters;
	FCallbackInitFunc InitFunc;


	bool bInitialized;
	TArray<int32> InitializedIndices;

	// Game thread collision geometry owned by THIS class.
	TArray< Chaos::TImplicitObject<float, 3>* > ImplicitObjects_GameThread;

	const FInitialState& GetInitialState() const { return InitialState; }

private:
	FInitialState InitialState;

};

#endif