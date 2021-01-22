// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsCoreTypes.h"
#include "Chaos/Defines.h"
#include "Chaos/EvolutionTraits.h"
#include "Chaos/PullPhysicsDataImp.h"

namespace Chaos
{
	template<typename T, int d>
	class TGeometryParticle;

	template <typename Traits>
	class TPBDRigidsEvolutionGBF;

	struct FDirtyRigidParticleData;
}

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

class CHAOS_API FSingleParticlePhysicsProxy : public IPhysicsProxyBase
{
public:
	using PARTICLE_TYPE = Chaos::TGeometryParticle<Chaos::FReal, 3>;
	using FParticleHandle = Chaos::TGeometryParticleHandle<Chaos::FReal,3>;

	FSingleParticlePhysicsProxy() = delete;
	FSingleParticlePhysicsProxy(PARTICLE_TYPE* InParticle, FParticleHandle* InHandle, UObject* InOwner = nullptr, FInitialState InitialState = FInitialState());
	virtual ~FSingleParticlePhysicsProxy();

	void SetPullDataInterpIdx_External(const int32 Idx)
	{
		PullDataInterpIdx_External = Idx;
	}

	int32 GetPullDataInterpIdx_External() const { return PullDataInterpIdx_External; }

	/**/
	const FInitialState& GetInitialState() const;

	FParticleHandle* GetHandle()
	{
		return Handle;
	}

	const FParticleHandle* GetHandle() const
	{
		return Handle;
	}

	virtual void* GetHandleUnsafe() const override
	{
		return Handle;
	}

	void SetHandle(FParticleHandle* InHandle)
	{
		Handle = InHandle;
	}

	void* GetUserData() const
	{
		auto GameThreadHandle = Handle->GTGeometryParticle();
		return GameThreadHandle ? GameThreadHandle->UserData() : nullptr;
	}

	Chaos::TRigidTransform<float, 3> GetTransform()
	{
		return Chaos::TRigidTransform<float, 3>(Handle->X(), Handle->R());
	}

	// Threading API

	template <typename Traits>
	void PushToPhysicsState(const Chaos::FDirtyPropertiesManager& Manager,int32 DataIdx,const Chaos::FDirtyProxy& Dirty,Chaos::FShapeDirtyData* ShapesData, Chaos::TPBDRigidsEvolutionGBF<Traits>& Evolution);

	/**/
	void ClearAccumulatedData();

	/**/
	void BufferPhysicsResults(Chaos::FDirtyRigidParticleData&);

	/**/
	void BufferPhysicsResults_External(Chaos::FDirtyRigidParticleData&);

	/**/
	bool PullFromPhysicsState(const Chaos::FDirtyRigidParticleData& PullData, int32 SolverSyncTimestamp, const Chaos::FDirtyRigidParticleData* NextPullData = nullptr, const float* Alpha = nullptr);

	/**/
	bool IsDirty();

	bool IsInitialized() const { return bInitialized; }
	void SetInitialized(bool InInitialized) { bInitialized = InInitialized; }

	/**/
	Chaos::EWakeEventEntry GetWakeEvent() const;

	/**/
	void ClearEvents();

	PARTICLE_TYPE* GetParticle()
	{
		return Particle;
	}

	const PARTICLE_TYPE* GetParticle() const
	{
		return Particle;
	}

	Chaos::TPBDRigidParticle<Chaos::FReal, 3>* GetRigidParticleUnsafe()
	{
		return static_cast<Chaos::TPBDRigidParticle<Chaos::FReal, 3>*>(GetParticle());
	}

	const Chaos::TPBDRigidParticle<Chaos::FReal, 3>* GetRigidParticleUnsafe() const
	{
		return static_cast<const Chaos::TPBDRigidParticle<Chaos::FReal, 3>*>(GetParticle());
	}

	/** Gets the owning external object for this solver object, never used internally */
	virtual UObject* GetOwner() const override { return Owner; }
	
private:
	bool bInitialized;
	TArray<int32> InitializedIndices;

private:
	FInitialState InitialState;

	PARTICLE_TYPE* Particle;
	FParticleHandle* Handle;

	UObject* Owner;

	//Used by interpolation code
	int32 PullDataInterpIdx_External;
};