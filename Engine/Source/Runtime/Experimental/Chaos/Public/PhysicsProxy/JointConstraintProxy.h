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
}

class FJointConstraintPhysicsProxy : public TPhysicsProxy<FJointConstraintPhysicsProxy,void>
{
	typedef TPhysicsProxy<FJointConstraintPhysicsProxy, void> Base;

public:
	using FReal = Chaos::FReal;
	using FConstraintHandle = typename Chaos::FJointConstraint::FHandle;
	using FConstraintData = typename Chaos::FJointConstraint::FData;
	using FJointConstraints = Chaos::FPBDJointConstraints;
	using FJointConstraintDirtyFlags = Chaos::FJointConstraintDirtyFlags;
	using FParticlePair = Chaos::TVec2<Chaos::FGeometryParticle*>;
	using FParticleHandlePair = Chaos::TVector<Chaos::TGeometryParticleHandle<FReal, 3>*, 2>;

	FJointConstraintPhysicsProxy() = delete;
	FJointConstraintPhysicsProxy(Chaos::FJointConstraint* InConstraint, FConstraintHandle* InHandle, UObject* InOwner = nullptr);

	EPhysicsProxyType ConcreteType()
	{
		return EPhysicsProxyType::JointConstraintType;
	}

	
	bool IsValid() { return Constraint != nullptr && Constraint->IsValid(); }

	bool IsInitialized() const { return bInitialized; }
	void SetInitialized() { bInitialized = true; }

	static Chaos::TGeometryParticleHandle<Chaos::FReal, 3>* GetParticleHandleFromProxy(IPhysicsProxyBase* ProxyBase);

	//
	//  Lifespan Management
	//

	void CHAOS_API InitializeOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver);

	// Merge to perform a remote sync
	void CHAOS_API PushStateOnGameThread(Chaos::FPBDRigidsSolver* InSolver);

	void CHAOS_API PushStateOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver);
	// Merge to perform a remote sync - END

	void CHAOS_API DestroyOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver);

	void SyncBeforeDestroy() {}
	void OnRemoveFromScene() {}


	//
	// Member Access
	//

	FConstraintHandle* GetHandle()
	{
		return Handle;
	}

	const FConstraintHandle* GetHandle() const
	{
		return Handle;
	}

	virtual void* GetHandleUnsafe() const override
	{
		return Handle;
	}

	void SetHandle(FConstraintHandle* InHandle)
	{
		Handle = InHandle;
	}

	Chaos::FJointConstraint* GetConstraint()
	{
		return Constraint;
	}

	const Chaos::FJointConstraint* GetConstraint() const
	{
		return Constraint;
	}

	//
	// Threading API
	//
	
	void PushToPhysicsState(Chaos::FPBDRigidsEvolutionGBF& Evolution) {}

	/**/
	void ClearAccumulatedData() {}

	/**/
	void BufferPhysicsResults(Chaos::FDirtyJointConstraintData& Buffer);

	/**/
	bool CHAOS_API PullFromPhysicsState(const Chaos::FDirtyJointConstraintData& Buffer, const int32 SolverSyncTimestamp);

	/**/
	bool IsDirty() { return Constraint->IsDirty(); }
	
private:

	// Input Buffer
	FConstraintData JointSettingsBuffer;
	FJointConstraintDirtyFlags DirtyFlagsBuffer;

	Chaos::FJointConstraint* Constraint; 	// This proxy assumes ownership of the Constraint, and will free it during DestroyOnPhysicsThread
	FConstraintHandle* Handle;
	bool bInitialized;


public:
	void AddForceCallback(FParticlesType& InParticles, const FReal InDt, const int32 InIndex) {}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32>& ParticleIDReverseMap) {}
	void EndFrameCallback(const FReal InDt) {}
	void ParameterUpdateCallback(FParticlesType& InParticles, const FReal InTime) {}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) {}
	bool IsSimulating() const { return true; }
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const FReal InDt, const FReal InTime, FKinematicProxy& InKinematicProxy) {}
	void StartFrameCallback(const FReal InDt, const FReal InTime) {}

};
