// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/GeometryParticleBuffer.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"
#include "Chaos/EvolutionTraits.h"
#include "Chaos/ParticleHandleFwd.h"

namespace Chaos
{

class FGeometryParticleProxy : public IPhysicsProxyBase
{
public:
	FGeometryParticleProxy(FGeometryParticleBuffer* InParticleBuffer, UObject* InOwner = nullptr);
	virtual ~FGeometryParticleProxy() = default;

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

private:
	bool bInitialized;

	FGeometryParticleBuffer* Buffer_External;
	FGeometryParticleBuffer* Buffer_Internal;
	TGeometryParticleHandle<FReal, 3>* Handle;

	//Used by interpolation code
	int32 PullDataInterpIdx_External;
};

}