// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

	// Called by FPBDRigidsSolver::RegisterObject(FFieldSystemPhysicsProxy*)
	void Initialize();

	// Callbacks
	bool IsSimulating() const;
	/**
	 * Services queued \c FFieldSystemCommand commands.
	 *
	 * Supported fields:
	 *	* EFieldPhysicsType::Field_DynamicState
	 *	* EFieldPhysicsType::Field_ActivateDisabled
	 *	* EFieldPhysicsType::Field_ExternalClusterStrain (clustering)
	 *	* EFieldPhysicsType::Field_Kill
	 *	* EFieldPhysicsType::Field_LinearVelocity
	 *	* EFieldPhysicsType::Field_AngularVelociy
	 *	* EFieldPhysicsType::Field_SleepingThreshold
	 *	* EFieldPhysicsType::Field_DisableThreshold
	 *	* EFieldPhysicsType::Field_InternalClusterStrain (clustering)
	 *	* EFieldPhysicsType::Field_CollisionGroup
	 *	* EFieldPhysicsType::Field_PositionStatic
	 *	* EFieldPhysicsType::Field_PositionTarget
	 *	* EFieldPhysicsType::Field_PositionAnimated
	 *	* EFieldPhysicsType::Field_DynamicConstraint
	 */
	void FieldParameterUpdateCallback(
		Chaos::FPhysicsSolver* InSolver, 
		FParticlesType& InParticles, 
		Chaos::TArrayCollectionArray<float>& Strains, 
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, 
		TMap<int32, int32>& PositionTargetedParticles, 
		//const TArray<FKinematicProxy>& AnimatedPositions, 
		const float InTime);

	/**
	 * Services queued \c FFieldSystemCommand commands.
	 *
	 * Supported fields:
	 *	* EFieldPhysicsType::Field_LinearForce
	 *	* EFieldPhysicsType::Field_AngularTorque
	 */
	void FieldForcesUpdateCallback(
		Chaos::FPhysicsSolver* InSolver, 
		FParticlesType& Particles, 
		Chaos::TArrayCollectionArray<FVector> & Force, 
		Chaos::TArrayCollectionArray<FVector> & Torque, 
		const float Time);

	void BufferCommand(Chaos::FPhysicsSolver* InSolver, const FFieldSystemCommand& InCommand);

	// Inactive Callbacks
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime){}
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy){}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper> & PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32> & ParticleIDReverseMap) {}
	void StartFrameCallback(const float InDt, const float InTime){}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles){}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs){}
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex){}
	void EndFrameCallback(const float InDt) { check(false); } // never called

	// Called by FPBDRigidsSolver::PushPhysicsState() on game thread.
	Chaos::FParticleData* NewData() { return nullptr; }
	// Called by FPBDRigidsSolver::PushPhysicsState() on physics thread.
	void PushToPhysicsState(const Chaos::FParticleData*) {};
	// Called by FPBDRigidsSolver::PushPhysicsState() on game thread.
	void ClearAccumulatedData() {}

	void BufferPhysicsResults(){}	// Not called
	void FlipBuffer(){}				// Not called
	void PullFromPhysicsState(){}	// Not called

	bool IsDirty() { return false; }
	void SyncBeforeDestroy(){}
	void OnRemoveFromScene();

	EPhysicsProxyType ConcreteType() {return EPhysicsProxyType::FieldType;}

	/**
	 * Generates a mapping between the Position array and the results array. 
	 *
	 * When \p ResolutionType is set to \c Maximum the complete particle mapping 
	 * is provided from the \c Particles.X to \c Particles.Attribute. 
	 * When \c Minimum is set only the ActiveIndices and the direct children of 
	 * the active clusters are set in the \p IndicesArray.
	 */
	static void ContiguousIndices(
		TArray<ContextIndex>& IndicesArray, 
		const Chaos::FPhysicsSolver* RigidSolver, 
		const EFieldResolutionType ResolutionType, 
		const bool bForce = true);

	static void GetParticleHandles(
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,
		const Chaos::FPhysicsSolver* RigidSolver,
		const EFieldResolutionType ResolutionType,
		const bool bForce = true);

private:

	TArray<FFieldSystemCommand>* GetSolverCommandList(const Chaos::FPhysicsSolver* InSolver);

	FCriticalSection CommandLock;
	TMap<Chaos::FPhysicsSolver*, TArray<FFieldSystemCommand>*> Commands;
};
