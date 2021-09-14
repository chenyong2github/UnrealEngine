// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"
#include "JointConstraintProxyFwd.h"

namespace Chaos
{
class FJointConstraint;

class FPBDRigidsEvolutionGBF;

struct FDirtyJointConstraintData;

class FJointConstraintPhysicsProxy : public IPhysicsProxyBase
{
	using Base = IPhysicsProxyBase;

public:
	FJointConstraintPhysicsProxy() = delete;
	FJointConstraintPhysicsProxy(FJointConstraint* InConstraint, FPBDJointConstraintHandle* InHandle, UObject* InOwner = nullptr);

	static FGeometryParticleHandle* GetParticleHandleFromProxy(IPhysicsProxyBase* ProxyBase);

	//
	//  Lifespan Management
	//

	void CHAOS_API InitializeOnPhysicsThread(FPBDRigidsSolver* InSolver, FDirtyPropertiesManager& Manager, int32 DataIdx, FDirtyChaosProperties& RemoteData);

	void CHAOS_API PushStateOnGameThread(FDirtyPropertiesManager& Manager, int32 DataIdx, FDirtyChaosProperties& RemoteData);

	void CHAOS_API PushStateOnPhysicsThread(FPBDRigidsSolver* InSolver, const FDirtyPropertiesManager& Manager, int32 DataIdx, const FDirtyChaosProperties& RemoteData);

	void CHAOS_API DestroyOnPhysicsThread(FPBDRigidsSolver* InSolver);

	void CHAOS_API DestroyOnGameThread();

	//
	// Member Access
	//

	FPBDJointConstraintHandle* GetHandle() { return Constraint_PT; }
	const FPBDJointConstraintHandle* GetHandle() const { return Constraint_PT; }

	virtual void* GetHandleUnsafe() const override { return Constraint_PT; }

	void SetHandle(FPBDJointConstraintHandle* InHandle)	{ Constraint_PT = InHandle; }

	FJointConstraint* GetConstraint(){ return Constraint_GT; }
	const FJointConstraint* GetConstraint() const { return Constraint_GT; }

	//
	// Threading API
	//
	
	/**/
	void BufferPhysicsResults(FDirtyJointConstraintData& Buffer);

	/**/
	bool CHAOS_API PullFromPhysicsState(const FDirtyJointConstraintData& Buffer, const int32 SolverSyncTimestamp);
	
private:
	FJointConstraint* Constraint_GT;
	FPBDJointConstraintHandle* Constraint_PT;
	bool bInitialized = false;
};

}