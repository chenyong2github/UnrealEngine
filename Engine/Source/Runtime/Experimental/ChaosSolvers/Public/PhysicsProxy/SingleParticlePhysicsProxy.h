// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Framework/MultiBufferResource.h"
#include "Chaos/Framework/PhysicsProxy.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsCoreTypes.h"

namespace Chaos
{
	template<typename T, int d>
	class TGeometryParticle;
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

/**
 * \p PARTICLE_TYPE is one of:
 *		\c Chaos::TGeometryParticle<float,3>
 *		\c Chaos::TKinematicGeometryParticle<float,3>
 *		\c Chaos::TPBDRigidParticle<float,3>
 */
template<class PARTICLE_TYPE>
class FSingleParticlePhysicsProxy : public TPhysicsProxy<FSingleParticlePhysicsProxy<PARTICLE_TYPE>, typename PARTICLE_TYPE::FData>
{
	typedef TPhysicsProxy<FSingleParticlePhysicsProxy<PARTICLE_TYPE>, typename PARTICLE_TYPE::FData> Base;

public:
	using FParticleHandle = typename PARTICLE_TYPE::FHandle;
	using FStorageData = typename PARTICLE_TYPE::FData;

	FSingleParticlePhysicsProxy() = delete;
	FSingleParticlePhysicsProxy(PARTICLE_TYPE* InParticle, FParticleHandle* InHandle, UObject* InOwner = nullptr, FInitialState InitialState = FInitialState());
	virtual ~FSingleParticlePhysicsProxy();

	// DELETE MOST OF ME
	void Initialize() {}
	bool IsSimulating() const { return true; }
	void ParameterUpdateCallback(FParticlesType& InParticles, const float InTime) {}
	void UpdateKinematicBodiesCallback(const FParticlesType& InParticles, const float InDt, const float InTime, FKinematicProxy& InKinematicProxy) {}
	void BindParticleCallbackMapping(Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyReverseMap, Chaos::TArrayCollectionArray<int32>& ParticleIDReverseMap) {}
	void StartFrameCallback(const float InDt, const float InTime) {}
	void EndFrameCallback(const float InDt) {}
	void CreateRigidBodyCallback(FParticlesType& InOutParticles) {}
	void DisableCollisionsCallback(TSet<TTuple<int32, int32>>& InPairs) {}
	void AddForceCallback(FParticlesType& InParticles, const float InDt, const int32 InIndex) {}
	void FieldForcesUpdateCallback(Chaos::FPhysicsSolver* InSolver, FParticlesType& Particles, Chaos::TArrayCollectionArray<FVector>& Force, Chaos::TArrayCollectionArray<FVector>& Torque, const float Time) {}
	void BufferCommand(Chaos::FPhysicsSolver* InSolver, const FFieldSystemCommand& InCommand) {}
	void SyncBeforeDestroy() {}
	void OnRemoveFromScene() {}
	// END DELETE ME

	/**/
	const FInitialState& GetInitialState() const;

	FParticleHandle* GetHandle()
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

	/** 
	 * Creates a copy of sim state and returns it as \c TGeometryParticleData, 
	 * \c TKinemticGeometr yParticleData, or \c TPBDRigidParticleData. 
	 */
	Chaos::FParticleData* NewData()
	{
		if (Particle)
			return Particle->NewData();
		return nullptr;
	}

	/**/
	EPhysicsProxyType ConcreteType() { return EPhysicsProxyType::NoneType; }

	// Threading API

	/**/
	void FlipBuffer() { BufferedData->FlipProducer(); }

	/**/
	void PushToPhysicsState(const Chaos::FParticleData*);

	/**/
	void ClearAccumulatedData();

	/**/
	void BufferPhysicsResults();

	/**/
	void PullFromPhysicsState();

	/**/
	bool IsDirty();

	/**/
	bool HasAwakeEvent() const;

	/**/
	void ClearEvents();

private:
	bool bInitialized;
	TArray<int32> InitializedIndices;

private:
	FInitialState InitialState;

	PARTICLE_TYPE* Particle;
	FParticleHandle* Handle;
	TUniquePtr<Chaos::IBufferResource<FStorageData>> BufferedData;
};


// TGeometryParticle specialization prototypes
template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearAccumulatedData();

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::BufferPhysicsResults();

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PullFromPhysicsState();

template< >
bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::IsDirty();

template< >
void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData);

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ConcreteType();

template< >
CHAOSSOLVERS_API bool FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::HasAwakeEvent() const;

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TGeometryParticle<float, 3>>::ClearEvents();

// TKinematicGeometryParticle specialization prototypes

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData);

template< >
void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearAccumulatedData();

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::BufferPhysicsResults();

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::PullFromPhysicsState();

template< >
bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::IsDirty();

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ConcreteType();

template< >
CHAOSSOLVERS_API bool FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::HasAwakeEvent() const;

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TKinematicGeometryParticle<float, 3>>::ClearEvents();

// TPBDRigidParticles specialization prototypes

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ClearAccumulatedData();

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::BufferPhysicsResults();

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PullFromPhysicsState();

template< >
bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::IsDirty();

template< >
void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::PushToPhysicsState(const Chaos::FParticleData* InData);

template< >
EPhysicsProxyType FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ConcreteType();

template< >
CHAOSSOLVERS_API bool FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::HasAwakeEvent() const;

template< >
CHAOSSOLVERS_API void FSingleParticlePhysicsProxy<Chaos::TPBDRigidParticle<float, 3>>::ClearEvents();


extern template class FSingleParticlePhysicsProxy< Chaos::TGeometryParticle<float, 3> >;
extern template class FSingleParticlePhysicsProxy< Chaos::TKinematicGeometryParticle<float, 3> >;
extern template class FSingleParticlePhysicsProxy< Chaos::TPBDRigidParticle<float,3> >;
