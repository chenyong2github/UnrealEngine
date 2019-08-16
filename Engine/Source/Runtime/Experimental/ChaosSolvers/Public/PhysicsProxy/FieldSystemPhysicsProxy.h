// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "Chaos/Framework/PhysicsProxy.h"
#include "Field/FieldSystem.h"
#include "Chaos/PBDPositionConstraints.h"

struct FKinematicProxy;

class FStubFieldData : public Chaos::FParticleData {
	void Reset() {};
};

class CHAOSSOLVERS_API FFieldSystemPhysicsProxy : public TPhysicsProxy<FFieldSystemPhysicsProxy, FStubFieldData>
{
	typedef TPhysicsProxy<FFieldSystemPhysicsProxy, FStubFieldData> Base;

public:
	FFieldSystemPhysicsProxy() = delete;
	FFieldSystemPhysicsProxy(UObject* InOwner);
	virtual ~FFieldSystemPhysicsProxy();

	// Callbacks
	bool IsSimulating() const;
	void FieldParameterUpdateCallback(Chaos::FPhysicsSolver* InSolver, FParticlesType& InParticles, Chaos::TArrayCollectionArray<float>& Strains, Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, TMap<int32, int32>& PositionTargetedParticles, const TArray<FKinematicProxy>& AnimatedPositions, const float InTime);
	void FieldForcesUpdateCallback(Chaos::FPhysicsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector> & Force, Chaos::TArrayCollectionArray<FVector> & Torque, const float Time);
	void EndFrameCallback(const float InDt);
	void BufferCommand(Chaos::FPhysicsSolver* InSolver, const FFieldSystemCommand& InCommand);

	// Inactive Callbacks
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime){}
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy){}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper> & PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap) {}
	void StartFrameCallback(const float InDt, const float InTime){}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles){}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs){}
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex){}

	Chaos::FParticleData* NewData() { return nullptr; }
	void SyncBeforeDestroy(){}
	void OnRemoveFromScene();
	void PushToPhysicsState(const Chaos::FParticleData*) {};
	void BufferPhysicsResults(){}
	void FlipBuffer(){}
	void PullFromPhysicsState(){}
	EPhysicsProxyType ConcreteType() {return EPhysicsProxyType::FieldType;}

	/*
	* ContiguousIndices
	*   Generates a mapping between the Position array and the results array. When EFieldResolutionType is set to Maximum the complete
	*   particle mapping is provided from the Particles.X to Particles.Attribute, when Minimum is set only the ActiveIndices and the
	*   direct children of the active clusters are set in the IndicesArray.
	*/
	static void ContiguousIndices(TArray<ContextIndex>& IndicesArray, const Chaos::FPhysicsSolver* RigidSolver, EFieldResolutionType ResolutionType, bool bForce);

private:

	TArray<FFieldSystemCommand>* GetSolverCommandList(const Chaos::FPhysicsSolver* InSolver);

	FCriticalSection CommandLock;
	TMap<Chaos::FPhysicsSolver*, TArray<FFieldSystemCommand>*> Commands;
};

#endif
