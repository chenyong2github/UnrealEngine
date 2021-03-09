// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/PBDSuspensionConstraints.h"
#include "Chaos/GeometryParticlesfwd.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"

namespace Chaos
{
	class FSuspensionConstraint;

	class FPBDRigidsEvolutionGBF;
}

class CHAOS_API FSuspensionConstraintPhysicsProxy : public TPhysicsProxy<FSuspensionConstraintPhysicsProxy,void>
{
	typedef TPhysicsProxy<FSuspensionConstraintPhysicsProxy, void> Base;

public:
	using FReal = Chaos::FReal;
	using FConstraintHandle = typename Chaos::FSuspensionConstraint::FHandle;
	using FConstraintData = typename Chaos::FSuspensionConstraint::FData;
	using FSuspensionConstraints = Chaos::FPBDSuspensionConstraints;
	using FSuspensionConstraintDirtyFlags = Chaos::FSuspensionConstraintDirtyFlags;
	using FParticlePair = Chaos::TVector<Chaos::FGeometryParticle*, 2>;
	using FParticleHandlePair = Chaos::TVector<Chaos::FGeometryParticleHandle*, 2>;

	FSuspensionConstraintPhysicsProxy() = delete;
	FSuspensionConstraintPhysicsProxy(Chaos::FSuspensionConstraint* InConstraint, FConstraintHandle* InHandle, UObject* InOwner = nullptr);

	EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::SuspensionConstraintType; }
	
	bool IsValid() { return Constraint != nullptr && Constraint->IsValid(); }

	bool IsInitialized() const { return bInitialized; }
	void SetInitialized() { bInitialized = true; }

	static Chaos::FGeometryParticleHandle* GetParticleHandleFromProxy(IPhysicsProxyBase* ProxyBase);

	//
	//  Lifespan Management
	//

	void InitializeOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver);

	// Merge to perform a remote sync
	void PushStateOnGameThread(Chaos::FPBDRigidsSolver* InSolver);

	void PushStateOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver);
	// Merge to perform a remote sync - END

	void DestroyOnPhysicsThread(Chaos::FPBDRigidsSolver* InSolver);

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

	Chaos::FSuspensionConstraint* GetConstraint()
	{
		return Constraint;
	}

	const Chaos::FSuspensionConstraint* GetConstraint() const
	{
		return Constraint;
	}

	//
	// Threading API
	//

	/**/
	void FlipBuffer() { }

	void PushToPhysicsState(Chaos::FPBDRigidsEvolutionGBF& Evolution) {}

	/**/
	void ClearAccumulatedData() {}

	/**/
	void BufferPhysicsResults() {}

	/**/
	void PullFromPhysicsState() {}

	/**/
	bool IsDirty() { return Constraint->IsDirty(); }
	
private:

	FConstraintData SuspensionSettingsBuffer;
	FSuspensionConstraintDirtyFlags DirtyFlagsBuffer;

	Chaos::FSuspensionConstraint* Constraint;
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
