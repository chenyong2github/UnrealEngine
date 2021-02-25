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
#include "Chaos/Core.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Framework/Threading.h"

namespace Chaos
{
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

class FRigidBodyHandle_External;
class FRigidBodyHandle_Internal;

class CHAOS_API FSingleParticlePhysicsProxy : public IPhysicsProxyBase
{
public:
	using PARTICLE_TYPE = Chaos::FGeometryParticle;
	using FParticleHandle = Chaos::TGeometryParticleHandle<Chaos::FReal,3>;

	static FSingleParticlePhysicsProxy* Create(TUniquePtr<Chaos::TGeometryParticle<Chaos::FReal, 3>>&& Particle);

	FSingleParticlePhysicsProxy() = delete;
	FSingleParticlePhysicsProxy(const FSingleParticlePhysicsProxy&) = delete;
	FSingleParticlePhysicsProxy(FSingleParticlePhysicsProxy&&) = delete;
	virtual ~FSingleParticlePhysicsProxy();

	void SetPullDataInterpIdx_External(const int32 Idx)
	{
		PullDataInterpIdx_External = Idx;
	}

	int32 GetPullDataInterpIdx_External() const { return PullDataInterpIdx_External; }

	FORCEINLINE Chaos::FRigidBodyHandle_External& GetGameThreadAPI()
	{
		return (Chaos::FRigidBodyHandle_External&)*this;
	}

	FORCEINLINE const Chaos::FRigidBodyHandle_External& GetGameThreadAPI() const
	{
		return (const Chaos::FRigidBodyHandle_External&)*this;
	}

	//Note this is a pointer because the internal handle may have already been deleted
	FORCEINLINE Chaos::FRigidBodyHandle_Internal* GetPhysicsThreadAPI()
	{
		return GetHandle_LowLevel() == nullptr ? nullptr : (Chaos::FRigidBodyHandle_Internal*)this;
	}

	//Note this is a pointer because the internal handle may have already been deleted
	FORCEINLINE const Chaos::FRigidBodyHandle_Internal* GetPhysicsThreadAPI() const
	{
		return GetHandle_LowLevel() == nullptr ? nullptr : (const Chaos::FRigidBodyHandle_Internal*)this;
	}

	/**/
	const FInitialState& GetInitialState() const;

	//Returns the underlying physics thread particle. Note this should only be needed for internal book keeping type tasks. API may change, use GetPhysicsThreadAPI instead
	FParticleHandle* GetHandle_LowLevel()
	{
		return Handle;
	}

	//Returns the underlying physics thread particle. Note this should only be needed for internal book keeping type tasks. API may change, use GetPhysicsThreadAPI instead
	const FParticleHandle* GetHandle_LowLevel() const
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

	//Returns the underlying game thread particle. Note this should only be needed for internal book keeping type tasks. API may change, use GetGameThreadAPI instead
	PARTICLE_TYPE* GetParticle_LowLevel()
	{
		return Particle.Get();
	}

	//Returns the underlying game thread particle. Note this should only be needed for internal book keeping type tasks. API may change, use GetGameThreadAPI instead
	const PARTICLE_TYPE* GetParticle_LowLevel() const
	{
		return Particle.Get();
	}

	Chaos::TPBDRigidParticle<Chaos::FReal, 3>* GetRigidParticleUnsafe()
	{
		return static_cast<Chaos::TPBDRigidParticle<Chaos::FReal, 3>*>(GetParticle_LowLevel());
	}

	const Chaos::TPBDRigidParticle<Chaos::FReal, 3>* GetRigidParticleUnsafe() const
	{
		return static_cast<const Chaos::TPBDRigidParticle<Chaos::FReal, 3>*>(GetParticle_LowLevel());
	}

	/** Gets the owning external object for this solver object, never used internally */
	virtual UObject* GetOwner() const override { return Owner; }
	
private:
	bool bInitialized;
	TArray<int32> InitializedIndices;

private:
	FInitialState InitialState;

protected:
	TUniquePtr<PARTICLE_TYPE> Particle;
	FParticleHandle* Handle;

private:

	UObject* Owner;

	//Used by interpolation code
	int32 PullDataInterpIdx_External;

	//use static Create
	FSingleParticlePhysicsProxy(TUniquePtr<PARTICLE_TYPE>&& InParticle, FParticleHandle* InHandle, UObject* InOwner = nullptr, FInitialState InitialState = FInitialState());
};

namespace Chaos
{

/** Wrapper class that routes all reads and writes to the appropriate particle data. This is helpful for cases where we want to both write to a particle and a network buffer for example*/
template <bool bExternal>
class TThreadedSingleParticlePhysicsProxyBase : protected FSingleParticlePhysicsProxy
{
	TThreadedSingleParticlePhysicsProxyBase() = delete;	//You should only ever new FSingleParticlePhysicsProxy, derrived types are simply there for API constraining, no new data
public:

	FSingleParticlePhysicsProxy* GetProxy() { return static_cast<FSingleParticlePhysicsProxy*>(this); }

	bool CanTreatAsKinematic() const
	{
		return Read([](auto* Particle) { return Particle->CastToKinematicParticle() != nullptr; });
	}

	bool CanTreatAsRigid() const
	{
		return Read([](auto* Particle) { return Particle->CastToRigidParticle() != nullptr; });
	}

	//API for static particle
	const FVec3& X() const { return ReadRef([](auto* Particle) -> const auto& { return Particle->X(); }); }
	void SetX(const FVec3& InX, bool bInvalidate = true) { Write([&InX, bInvalidate](auto* Particle) { Particle->SetX(InX, bInvalidate); });}

	FUniqueIdx UniqueIdx() const { return Read([](auto* Particle) { return Particle->UniqueIdx(); }); }
	void SetUniqueIdx(const FUniqueIdx UniqueIdx, bool bInvalidate = true) { Write([UniqueIdx, bInvalidate](auto* Particle) { Particle->SetUniqueIdx(UniqueIdx, bInvalidate); }); }

	const FRotation3& R() const { return ReadRef([](auto* Particle) -> const auto& { return Particle->R(); }); }
	void SetR(const FRotation3& InR, bool bInvalidate = true) { Write([&InR, bInvalidate](auto* Particle) { Particle->SetR(InR, bInvalidate); }); }

	const TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>& SharedGeometryLowLevel() const { return ReadRef([](auto* Ptr) -> const auto& { return Ptr->SharedGeometryLowLevel(); });}

#if CHAOS_CHECKED
	const FName DebugName() const { return Read([](auto* Ptr) { return Ptr->DebugName(); }); }
	void SetDebugName(const FName& InDebugName) { Write([&InDebugName](auto* Ptr) { Ptr->SetDebugName(InDebugName); }); }
#endif

	TSerializablePtr<FImplicitObject> Geometry() const { return Read([](auto* Ptr) { return Ptr->Geometry(); }); }

	const FShapesArray& ShapesArray() const { return ReadRef([](auto* Ptr) -> const auto& { return Ptr->ShapesArray(); }); }

	EObjectStateType ObjectState() const { return Read([](auto* Ptr) { return Ptr->ObjectState(); }); }

	EParticleType ObjectType() const { return Read([](auto* Ptr) { return Ptr->ObjectType(); }); }

	FSpatialAccelerationIdx SpatialIdx() const { return Read([](auto* Ptr) { return Ptr->SpatialIdx(); }); }
	void SetSpatialIdx(FSpatialAccelerationIdx Idx) { Write([Idx](auto* Ptr) { Ptr->SetSpatialIdx(Idx); }); }

	//API for kinematic particle
	const FVec3 V() const
	{
		return Read([](auto* Particle)
		{
			if (auto Kinematic = Particle->CastToKinematicParticle())
			{
				return Kinematic->V();
			}
			
			return FVec3(0);
		});
	}

	void SetV(const FVec3& InV, bool bInvalidate = true)
	{
		Write([&InV, bInvalidate](auto* Particle)
		{
			if (auto Kinematic = Particle->CastToKinematicParticle())
			{
				return Kinematic->SetV(InV, bInvalidate);
			}
		});
	}

	const FVec3 W() const
	{
		return Read([](auto* Particle)
		{
			if (auto Kinematic = Particle->CastToKinematicParticle())
			{
				return Kinematic->W();
			}

			return FVec3(0);
		});
	}

	void SetW(const FVec3& InW, bool bInvalidate = true)
	{
		Write([&InW, bInvalidate](auto* Particle)
		{
			if (auto Kinematic = Particle->CastToKinematicParticle())
			{
				return Kinematic->SetW(InW, bInvalidate);
			}
		});
	}

	void SetKinematicTarget(const TKinematicTarget<FReal, 3>& InKinematicTarget, bool bInvalidate = true)
	{
		Write([&InKinematicTarget, bInvalidate](auto* Ptr)
		{
			if (auto Kinematic = Ptr->CastToKinematicParticle())
			{
				Kinematic->SetKinematicTarget(InKinematicTarget, bInvalidate);
			}
		});
	}
	
	//API for dynamic particle

	bool GravityEnabled() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->GravityEnabled();
			}

			return false;
		});
	}

	void SetGravityEnabled(const bool InGravityEnabled)
	{
		Write([InGravityEnabled](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->SetGravityEnabled(InGravityEnabled);
			}
		});
	}

	bool CCDEnabled() const
	{
		return Read([](auto* Particle)
			{
				if (auto Rigid = Particle->CastToRigidParticle())
				{
					return Rigid->CCDEnabled();
				}

				return false;
			});
	}

	void SetCCDEnabled(const bool InCCDEnabled)
	{
		Write([InCCDEnabled](auto* Particle)
			{
				if (auto Rigid = Particle->CastToRigidParticle())
				{
					return Rigid->SetCCDEnabled(InCCDEnabled);
				}
			});
	}

	bool OneWayInteraction() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->OneWayInteraction();
			}

			return false;
		});
	}

	void SetOneWayInteraction(const bool InOneWayInteraction)
	{
		Write([InOneWayInteraction](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->SetOneWayInteraction(InOneWayInteraction);
			}
		});
	}

	void SetResimType(EResimType ResimType)
	{
		Write([ResimType](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->SetResimType(ResimType);
			}
		});
	}

	EResimType ResimType() const
	{
		if (auto Rigid = Particle->CastToRigidParticle())
		{
			return Rigid->ResimType();
		}

		return EResimType::FullResim;
	}

	const FVec3 F() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->F();
			}

			return FVec3(0);
		});
	}

	void AddForce(const FVec3& InForce, bool bInvalidate = true)
	{
		Write([&InForce, bInvalidate](auto* Particle)
		{
			if (auto* Rigid = Particle->CastToRigidParticle())
			{
				Rigid->AddForce(InForce, bInvalidate);
			}
		});
	}

	const FVec3 Torque() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->Torque();
			}

			return FVec3(0);
		});
	}

	void AddTorque(const FVec3& InTorque, bool bInvalidate = true)
	{
		Write([&InTorque, bInvalidate](auto* Particle)
		{
			if (auto* Rigid = Particle->CastToRigidParticle())
			{
				Rigid->AddTorque(InTorque, bInvalidate);
			}
		});
	}

	const FVec3 LinearImpulse() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->LinearImpulse();
			}

			return FVec3(0);
		});
	}

	void SetLinearImpulse(const FVec3& InLinearImpulse, bool bInvalidate = true)
	{
		Write([&InLinearImpulse, bInvalidate](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetLinearImpulse(InLinearImpulse, bInvalidate);
			}
		});
	}

	const FVec3 AngularImpulse() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->AngularImpulse();
			}

			return FVec3(0);
		});
	}

	void SetAngularImpulse(const FVec3& InAngularImpulse, bool bInvalidate = true)
	{
		Write([&InAngularImpulse, bInvalidate](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetAngularImpulse(InAngularImpulse, bInvalidate);
			}
		});
	}

	const PMatrix<FReal,3,3> I() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->I();
			}

			return PMatrix<FReal,3,3>(0, 0, 0);
		});
	}

	void SetI(const PMatrix<FReal,3,3>& InI)
	{
		Write([&InI](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetI(InI);
			}
		});
	}

	const PMatrix<FReal, 3, 3> InvI() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->InvI();
			}

			return PMatrix<FReal, 3, 3>(0, 0, 0);
		});
	}

	void SetInvI(const PMatrix<FReal, 3, 3>& InInvI)
	{
		Write([&InInvI](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetInvI(InInvI);
			}
		});
	}

	const FReal M() const
	{
		return Read([](auto* Particle) -> FReal
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->M();
			}

			return 0;
		});
	}

	void SetM(const FReal InM)
	{
		Write([&InM](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetM(InM);
			}
		});
	}

	const FReal InvM() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->InvM();
			}

			return 0;
		});
	}

	void SetInvM(const FReal InInvM)
	{
		Write([&InInvM](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetInvM(InInvM);
			}
		});
	}

	const FVec3 CenterOfMass() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->CenterOfMass();
			}

			return FVec3(0);
		});
	}

	void SetCenterOfMass(const FVec3& InCenterOfMass, bool bInvalidate = true)
	{
		Write([&InCenterOfMass, bInvalidate](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetCenterOfMass(InCenterOfMass, bInvalidate);
			}
		});
	}

	const FRotation3 RotationOfMass() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->RotationOfMass();
			}

			return FRotation3::FromIdentity();
		});
	}

	void SetRotationOfMass(const FRotation3& InRotationOfMass, bool bInvalidate = true)
	{
		Write([&InRotationOfMass, bInvalidate](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetRotationOfMass(InRotationOfMass, bInvalidate);
			}
		});
	}

	const FReal LinearEtherDrag() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->LinearEtherDrag();
			}

			return 0;
		});
	}

	void SetLinearEtherDrag(const FReal InLinearEtherDrag)
	{
		Write([&InLinearEtherDrag](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetLinearEtherDrag(InLinearEtherDrag);
			}
		});
	}

	const FReal AngularEtherDrag() const
	{
		return Read([](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				return Rigid->AngularEtherDrag();
			}

			return 0;
		});
	}

	void SetAngularEtherDrag(const FReal InAngularEtherDrag)
	{
		Write([&InAngularEtherDrag](auto* Particle)
		{
			if (auto Rigid = Particle->CastToRigidParticle())
			{
				Rigid->SetAngularEtherDrag(InAngularEtherDrag);
			}
		});
	}

	void SetObjectState(const EObjectStateType InState, bool bAllowEvents = false, bool bInvalidate = true)
	{
		Write([InState, bAllowEvents, bInvalidate](auto* Ptr)
		{
			if (auto Rigid = Ptr->CastToRigidParticle())
			{
				Rigid->SetObjectState(InState, bAllowEvents, bInvalidate);
			}
		});
	}

protected:
	void VerifyContext() const
	{
#if PHYSICS_THREAD_CONTEXT
		//Are you using the wrong API type for the thread this code runs in?
		//GetGameThreadAPI should be used for gamethread, GetPhysicsThreadAPI should be used for callbacks and internal physics thread
		//Note if you are using a ParallelFor you must use PhysicsParallelFor to ensure the right context is inherited from parent thread
		if(bExternal)
		{
			//if proxy is registered with solver, we need a lock
			if(GetSolverBase() != nullptr)
			{
				ensure(IsInGameThreadContext());
			}
		}
		else
		{
			ensure(IsInPhysicsThreadContext());
		}
#endif
	}

private:

	template <typename TLambda>
	auto Read(const TLambda& Lambda) const { VerifyContext(); return bExternal ? Lambda(GetParticle_LowLevel()) : Lambda(GetHandle_LowLevel()); }

	template <typename TLambda>
	const auto& ReadRef(const TLambda& Lambda) const { VerifyContext(); return bExternal ? Lambda(GetParticle_LowLevel()) : Lambda(GetHandle_LowLevel()); }

	template <typename TLambda>
	auto& ReadRef(const TLambda& Lambda) { VerifyContext(); return bExternal ? Lambda(GetParticle_LowLevel()) : Lambda(GetHandle_LowLevel()); }

	template <typename TLambda>
	void Write(const TLambda& Lambda)
	{
		VerifyContext();
		if (bExternal)
		{
			Lambda(GetParticle_LowLevel());
		}
		else
		{
			Lambda(GetHandle_LowLevel());
			//todo: write to extra buffer
		}
	}
};

static_assert(sizeof(TThreadedSingleParticlePhysicsProxyBase<true>) == sizeof(FSingleParticlePhysicsProxy), "Derived types only used to constrain API, all data lives in base class ");
static_assert(sizeof(TThreadedSingleParticlePhysicsProxyBase<false>) == sizeof(FSingleParticlePhysicsProxy), "Derived types only used to constrain API, all data lives in base class ");


class FRigidBodyHandle_External : public TThreadedSingleParticlePhysicsProxyBase<true>
{
	FRigidBodyHandle_External() = delete;	//You should only ever new FSingleParticlePhysicsProxy, derrived types are simply there for API constraining, no new data

public:
	using Base = TThreadedSingleParticlePhysicsProxyBase<true>;
	using Base::VerifyContext;

	void SetIgnoreAnalyticCollisions(bool bIgnoreAnalyticCollisions) { VerifyContext(); GetParticle_LowLevel()->SetIgnoreAnalyticCollisions(bIgnoreAnalyticCollisions); }
	void UpdateShapeBounds() { VerifyContext(); GetParticle_LowLevel()->UpdateShapeBounds(); }

	void UpdateShapeBounds(const FTransform& Transform) { VerifyContext(); GetParticle_LowLevel()->UpdateShapeBounds(Transform); }

	void SetShapeCollisionTraceType(int32 InShapeIndex, EChaosCollisionTraceFlag TraceType) { VerifyContext(); GetParticle_LowLevel()->SetShapeCollisionTraceType(InShapeIndex, TraceType); }

	void SetShapeSimCollisionEnabled(int32 InShapeIndex, bool bInEnabled) { VerifyContext(); GetParticle_LowLevel()->SetShapeSimCollisionEnabled(InShapeIndex, bInEnabled); }
	void SetShapeQueryCollisionEnabled(int32 InShapeIndex, bool bInEnabled) { VerifyContext(); GetParticle_LowLevel()->SetShapeQueryCollisionEnabled(InShapeIndex, bInEnabled); }
	void SetShapeSimData(int32 InShapeIndex, const FCollisionFilterData& SimData) { VerifyContext(); GetParticle_LowLevel()->SetShapeSimData(InShapeIndex, SimData); }


	int32 Island() const
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			return Rigid->Island();
		}

		return INDEX_NONE;
	}
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetIsland(const int32 InIsland)
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			Rigid->SetIsland(InIsland);
		}
	}

	bool ToBeRemovedOnFracture() const
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			return Rigid->ToBeRemovedOnFracture();
		}

		return false;
	}
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetToBeRemovedOnFracture(const bool InToBeRemovedOnFracture)
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			Rigid->SetToBeRemovedOnFracture(InToBeRemovedOnFracture);
		}
	}

	void ClearEvents()
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			Rigid->ClearEvents();
		}
	}

	EWakeEventEntry GetWakeEvent()
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			return Rigid->GetWakeEvent();
		}

		return EWakeEventEntry::None;
	}

	void ClearForces(bool bInvalidate = true)
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			Rigid->ClearForces(bInvalidate);
		}
	}

	void ClearTorques(bool bInvalidate = true)
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			Rigid->ClearTorques(bInvalidate);
		}
	}

	void* UserData() const { VerifyContext(); return GetParticle_LowLevel()->UserData(); }
	void SetUserData(void* InUserData) { VerifyContext(); GetParticle_LowLevel()->SetUserData(InUserData); }


	//todo: geometry should not be owned by particle
	void SetGeometry(TUniquePtr<FImplicitObject>&& UniqueGeometry)
	{
		VerifyContext();
		FImplicitObject* RawGeometry = UniqueGeometry.Release();
		SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(RawGeometry));
	}

	void SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> SharedGeometry)
	{
		VerifyContext();
		GetParticle_LowLevel()->SetGeometry(SharedGeometry);
	}

	//Note: this must be called after setting geometry. This API seems bad. Should probably be part of setting geometry
	void SetShapesArray(FShapesArray&& InShapesArray) { VerifyContext(); GetParticle_LowLevel()->SetShapesArray(MoveTemp(InShapesArray)); }

	void RemoveShape(FPerShapeData* InShape, bool bWakeTouching) { VerifyContext(); GetParticle_LowLevel()->RemoveShape(InShape, bWakeTouching); }

	void MergeShapesArray(FShapesArray&& OtherShapesArray) { VerifyContext(); GetParticle_LowLevel()->MergeShapesArray(MoveTemp(OtherShapesArray)); }

	void MergeGeometry(TArray<TUniquePtr<FImplicitObject>>&& Objects) { VerifyContext(); GetParticle_LowLevel()->MergeGeometry(MoveTemp(Objects)); }

	void SetCCDEnabled(bool bEnabled)
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			Rigid->SetCCDEnabled(bEnabled);
		}
	}

	bool CCDEnabled() const
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			return Rigid->CCDEnabled();
		}

		return false;
	}

	void SetDisabled(bool bDisable)
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			Rigid->SetDisabled(bDisable);
		}
	}

	bool Disabled() const
	{
		VerifyContext();
		if (auto Rigid = GetParticle_LowLevel()->CastToRigidParticle())
		{
			return Rigid->Disabled();
		}

		return false;
	}

};

static_assert(sizeof(FRigidBodyHandle_External) == sizeof(FSingleParticlePhysicsProxy), "Derived types only used to constrain API, all data lives in base class ");


class FRigidBodyHandle_Internal : public TThreadedSingleParticlePhysicsProxyBase<false>
{
	FRigidBodyHandle_Internal() = delete;	//You should only ever new FSingleParticlePhysicsProxy, derived types are simply there for API constraining, no new data

public:
	using Base = TThreadedSingleParticlePhysicsProxyBase<false>;

	const FVec3 PreV() const
	{
		VerifyContext();
		if (auto Rigid = GetHandle_LowLevel()->CastToRigidParticle())
		{
			return Rigid->PreV();
		}
		return FVec3(0);
	}

	const FVec3 PreW() const
	{
		VerifyContext();
		if (auto Rigid = GetHandle_LowLevel()->CastToRigidParticle())
		{
			return Rigid->PreW();
		}
		return FVec3(0);
	}
};

static_assert(sizeof(FRigidBodyHandle_Internal) == sizeof(FSingleParticlePhysicsProxy), "Derived types only used to constrain API, all data lives in base class ");

}

inline FSingleParticlePhysicsProxy* FSingleParticlePhysicsProxy::Create(TUniquePtr<Chaos::TGeometryParticle<Chaos::FReal, 3>>&& Particle)
{
	ensure(Particle->GetProxy() == nullptr);	//not already owned by another proxy. TODO: use TUniquePtr
	auto Proxy = new FSingleParticlePhysicsProxy(MoveTemp(Particle), nullptr);
	return Proxy;
}