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
		
	bool IsValid() { return Constraint && Constraint->IsValid(); }

	bool IsInitialized() const { return bInitialized; }
	void SetInitialized() { bInitialized = true; }

	static FGeometryParticleHandle* GetParticleHandleFromProxy(IPhysicsProxyBase* ProxyBase);

	//
	//  Lifespan Management
	//

	void CHAOS_API InitializeOnPhysicsThread(FPBDRigidsSolver* InSolver);

	// Merge to perform a remote sync
	void CHAOS_API PushStateOnGameThread(FPBDRigidsSolver* InSolver);

	void CHAOS_API PushStateOnPhysicsThread(FPBDRigidsSolver* InSolver);
	// Merge to perform a remote sync - END

	void CHAOS_API DestroyOnPhysicsThread(FPBDRigidsSolver* InSolver);

	//
	// Member Access
	//

	FPBDJointConstraintHandle* GetHandle() { return Handle; }
	const FPBDJointConstraintHandle* GetHandle() const { return Handle; }

	virtual void* GetHandleUnsafe() const override { return Handle; }

	void SetHandle(FPBDJointConstraintHandle* InHandle)	{ Handle = InHandle; }

	FJointConstraint* GetConstraint(){ return Constraint; }
	const FJointConstraint* GetConstraint() const { return Constraint; }

	//
	// Threading API
	//
	
	/**/
	void BufferPhysicsResults(FDirtyJointConstraintData& Buffer);

	/**/
	bool CHAOS_API PullFromPhysicsState(const FDirtyJointConstraintData& Buffer, const int32 SolverSyncTimestamp);

	/**/
	bool IsDirty() { return Constraint->IsDirty(); }
	
private:

	// Input Buffer
	FPBDJointSettings JointSettingsBuffer;
	FJointConstraintDirtyFlags DirtyFlagsBuffer;

	FJointConstraint* Constraint; 	// This proxy assumes ownership of the Constraint, and will free it during DestroyOnPhysicsThread
	FPBDJointConstraintHandle* Handle;
	bool bInitialized = false;
};

}