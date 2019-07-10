// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "SolverObject.h"
#include "Field/FieldSystem.h"
#include "Chaos/PBDPositionConstraints.h"
#include "PBDRigidsSolver.h"

struct FKinematicProxy;

class CHAOSSOLVERS_API FFieldSystemPhysicsObject : public TSolverObject<FFieldSystemPhysicsObject>
{
public:

	FFieldSystemPhysicsObject() = delete;
	FFieldSystemPhysicsObject(UObject* InOwner);
	virtual ~FFieldSystemPhysicsObject();

	// Callbacks
	bool IsSimulating() const;
	void FieldParameterUpdateCallback(Chaos::FPBDRigidsSolver* InSolver, FParticlesType& InParticles, Chaos::TArrayCollectionArray<float>& Strains, Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, TMap<int32, int32>& PositionTargetedParticles, const TArray<FKinematicProxy>& AnimatedPositions, const float InTime);
	void FieldForcesUpdateCallback(Chaos::FPBDRigidsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector> & Force, Chaos::TArrayCollectionArray<FVector> & Torque, const float Time);
	void EndFrameCallback(const float InDt);
	void BufferCommand(Chaos::FPBDRigidsSolver* InSolver, const FFieldSystemCommand& InCommand);

	// Inactive Callbacks
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime){}
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy){}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<SolverObjectWrapper> & SolverObjectReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap) {}
	void StartFrameCallback(const float InDt, const float InTime){}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles){}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs){}
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex){}

	void SyncBeforeDestroy(){}
	void OnRemoveFromScene();
	void CacheResults(){}
	void FlipCache(){}
	void SyncToCache(){}

	/*
	* ContiguousIndices
	*   Generates a mapping between the Position array and the results array. When EFieldResolutionType is set to Maximum the complete
	*   particle mapping is provided from the Particles.X to Particles.Attribute, when Minimum is set only the ActiveIndices and the
	*   direct children of the active clusters are set in the IndicesArray.
	*/
	static void ContiguousIndices(TArray<ContextIndex>& IndicesArray, const Chaos::FPBDRigidsSolver* RigidSolver, EFieldResolutionType ResolutionType, bool bForce);

private:

	TArray<FFieldSystemCommand>* GetSolverCommandList(const Chaos::FPBDRigidsSolver* InSolver);

	FCriticalSection CommandLock;
	TMap<Chaos::FPBDRigidsSolver*, TArray<FFieldSystemCommand>*> Commands;
};

#endif
