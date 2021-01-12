// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/PBDRigidParticleBuffer.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"
#include "Chaos/EvolutionTraits.h"
#include "Chaos/ParticleHandleFwd.h"

namespace Chaos
{

/**
* Class for managing read/write operations on physics particles from multiple threads.
* This works by returning a buffer for the user to read/write. The buffer is a transient object and should not be held for long
* The buffer pointer will change for certain non-trivial operations (changing underlying type from kinematic to dynamic, disabling the particle, etc...)
* As such you should only hold on to a buffer in a local function where you know you are only doing trivial operations (set position, read velocity, etc...)
**/
class FSingleParticleProxy : public IPhysicsProxyBase
{
public:
	FSingleParticleProxy(TUniquePtr<FGeometryParticleBuffer>&& InParticleBuffer)
	: IPhysicsProxyBase(EPhysicsProxyType::SingleParticleProxy)
	, Buffer_External(MoveTemp(InParticleBuffer))
	{
		if(Buffer_External)
		{
			Buffer_External->SetProxy(this);
		}
	}

	//Get transient geometry particle buffer
	const FGeometryParticleBuffer* GetTransientBuffer() const
	{
		//todo: handle threads
		return Buffer_External.Get();
	}

	//Get transient geometry particle buffer
	FGeometryParticleBuffer* GetTransientBuffer()
	{
		//todo: handle threads
		return Buffer_External.Get();
	}

	//Get transient kinematic geometry particle buffer (do not hold on to buffer unless you are doing trivial operations like setting properties, reading values, etc...)
	const FKinematicGeometryParticleBuffer* GetTransientKinematicBuffer() const { return FKinematicGeometryParticleBuffer::Cast(GetTransientBuffer()); }
	FKinematicGeometryParticleBuffer* GetTransientKinematicBuffer() { return FKinematicGeometryParticleBuffer::Cast(GetTransientBuffer()); }
	
	//Get transient pbdrigid particle buffer (do not hold on to buffer unless you are doing trivial operations like setting properties, reading values, etc...)
	const FPBDRigidParticleBuffer* GetTransientPBDRigidBuffer() const { return FPBDRigidParticleBuffer::Cast(GetTransientBuffer()); }
	FPBDRigidParticleBuffer* GetTransientPBDRigidBuffer() { return FPBDRigidParticleBuffer::Cast(GetTransientBuffer()); }
	
	void SetPullDataInterpIdx_External(const int32 Idx)
	{
		PullDataInterpIdx_External = Idx;
	}

	int32 GetPullDataInterpIdx_External() const
	{
		return PullDataInterpIdx_External;
	}

	TGeometryParticleHandle<FReal, 3>* GetHandle()
	{
		return Handle;
	}

	const TGeometryParticleHandle<FReal, 3>* GetHandle() const
	{
		return Handle;
	}

	virtual void* GetHandleUnsafe() const override
	{
		check(false);
		return nullptr;
	}

	void SetHandle(TGeometryParticleHandle<FReal, 3>* InHandle)
	{
		Handle = InHandle;
	}

	template <typename Traits>
	void PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager, int32 DataIdx, const Chaos::FDirtyProxy& Dirty, Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution);

	/**/
	bool IsDirty();

	bool IsInitialized() const { return bInitialized; }
	void SetInitialized(bool InInitialized) { bInitialized = InInitialized; }

	//should this function exist?
	virtual UObject* GetOwner() const override { return nullptr; }

private:
	bool bInitialized;

	TUniquePtr<FGeometryParticleBuffer> Buffer_External;
	TUniquePtr<FGeometryParticleBuffer> Buffer_Internal;
	TGeometryParticleHandle<FReal, 3>* Handle;

	//Used by interpolation code
	int32 PullDataInterpIdx_External;
};

}