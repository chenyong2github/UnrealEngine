// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/ChooseClass.h"
#include "Chaos/PBDRigidClusteredParticles.h"
#include "Chaos/PBDGeometryCollectionParticles.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ParticleIterator.h"
#include "Chaos/ParticleDirtyFlags.h"
#include "Chaos/Framework/PhysicsProxyBase.h"
#include "ChaosCheck.h"

class IPhysicsProxyBase;

namespace Chaos
{


template <typename T, int d>
struct TGeometryParticleParameters
{
	TGeometryParticleParameters()
		: bDisabled(false) {}
	bool bDisabled;
};

template <typename T, int d>
struct TKinematicGeometryParticleParameters : public TGeometryParticleParameters<T, d>
{
	TKinematicGeometryParticleParameters()
		: TGeometryParticleParameters<T, d>() {}
};

template <typename T, int d>
struct TPBDRigidParticleParameters : public TKinematicGeometryParticleParameters<T, d>
{
	TPBDRigidParticleParameters()
		: TKinematicGeometryParticleParameters<T, d>()
		, bStartSleeping(false)
		, bGravityEnabled(false)
	{}
	bool bStartSleeping;
	bool bGravityEnabled;
};

/** Concrete can either be the game thread or physics representation, but API stays the same. Useful for keeping initialization and other logic the same*/
template <typename T, int d, typename FConcrete>
void GeometryParticleDefaultConstruct(FConcrete& Concrete, const TGeometryParticleParameters<T,d>& Params)
{
	Concrete.SetX(TVector<T, d>(0));
	Concrete.SetR(TRotation<T, d>::Identity);
	Concrete.SetSpatialIdx(FSpatialAccelerationIdx{ 0,0 });
	Concrete.SetHashResultLowLevel(FMath::RandRange(0, FMath::RandHelper(TNumericLimits<int32>::Max())));
}

template <typename T, int d, typename FConcrete>
void KinematicGeometryParticleDefaultConstruct(FConcrete& Concrete, const TKinematicGeometryParticleParameters<T, d>& Params)
{
	Concrete.SetV(TVector<T, d>(0));
	Concrete.SetW(TVector<T, d>(0));
	Concrete.SetCenterOfMass(TVector<T, d>(0));
	Concrete.SetRotationOfMass(TRotation<T, d>(FQuat(EForceInit::ForceInit)));
}

template <typename T, int d, bool bPersistent>
void PBDRigidParticleHandleImpDefaultConstruct(TPBDRigidParticleHandleImp<T, d, bPersistent>& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
{
	//don't bother calling parent since the call gets made by the corresponding hierarchy in FConcrete
	Concrete.SetCollisionGroup(0);
	Concrete.SetDisabled(Params.bDisabled);
	Concrete.SetPreV(Concrete.V());
	Concrete.SetPreW(Concrete.W());
	Concrete.SetP(Concrete.X());
	Concrete.SetQ(Concrete.R());
	Concrete.SetF(TVector<T, d>(0));
	Concrete.SetTorque(TVector<T, d>(0));
	Concrete.SetExternalForce(TVector<T, d>(0));
	Concrete.SetExternalTorque(TVector<T, d>(0));
	Concrete.SetM(1);
	Concrete.SetInvM(1);
	Concrete.SetI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetInvI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetObjectStateLowLevel(Params.bStartSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic);
}

template <typename T, int d>
void PBDRigidParticleDefaultConstruct(TPBDRigidParticle<T,d>& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
{
	//don't bother calling parent since the call gets made by the corresponding hierarchy in FConcrete
	Concrete.SetCollisionGroup(0);
	Concrete.SetDisabled(Params.bDisabled);
	Concrete.SetPreV(Concrete.V());
	Concrete.SetPreW(Concrete.W());
	Concrete.SetP(Concrete.X());
	Concrete.SetQ(Concrete.R());
	Concrete.SetF(TVector<T, d>(0));
	Concrete.SetTorque(TVector<T, d>(0));
	Concrete.SetExternalForce(TVector<T, d>(0));
	Concrete.SetExternalTorque(TVector<T, d>(0));
	Concrete.SetM(1);
	Concrete.SetInvM(1);
	Concrete.SetI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetInvI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetObjectState(Params.bStartSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic);
	Concrete.SetGravityEnabled(Params.bGravityEnabled);
}



template <typename T, int d, typename FConcrete>
void PBDRigidClusteredParticleDefaultConstruct(FConcrete& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
{
	//don't bother calling parent since the call gets made by the corresponding hierarchy in FConcrete
}

template <typename FConcrete>
bool GeometryParticleSleeping(const FConcrete& Concrete)
{
	return Concrete.ObjectState() == EObjectStateType::Sleeping;
}

/** Wrapper that holds both physics thread data and GT data. It's possible that the physics handle is null if we're doing operations entirely on external threads*/
template <typename T, int d>
class TAccelerationStructureHandle
{
public:
	TAccelerationStructureHandle(TGeometryParticleHandle<T, d>* InHandle);
	TAccelerationStructureHandle(TGeometryParticle<T, d>* InGeometryParticle = nullptr);

	template <bool bPersistent>
	TAccelerationStructureHandle(TGeometryParticleHandleImp<T, d, bPersistent>& InHandle);

	template <bool bPersistent>
	TAccelerationStructureHandle(TGeometryParticleHandleImp<T, d, bPersistent>* InHandle, TGeometryParticle<T, d>* InGeometryParticle);


	//Should only be used by GT and scene query threads where an appropriate lock has been acquired
	TGeometryParticle<T, d>* GetExternalGeometryParticle_ExternalThread() const { return ExternalGeometryParticle; }

	//Should only be used by PT
	TGeometryParticleHandle<T, d>* GetGeometryParticleHandle_PhysicsThread() const { return GeometryParticleHandle; }

	bool operator==(const TAccelerationStructureHandle<T, d>& Rhs) const
	{
		CHAOS_ENSURE((ExternalGeometryParticle || GeometryParticleHandle));
		CHAOS_ENSURE((Rhs.ExternalGeometryParticle || Rhs.GeometryParticleHandle));

		if (!GeometryParticleHandle || !Rhs.GeometryParticleHandle)
		{
			return (ExternalGeometryParticle == Rhs.ExternalGeometryParticle);
		}
		else
		{
			return (GeometryParticleHandle == Rhs.GeometryParticleHandle);
		}
	}

	bool operator!=(const TAccelerationStructureHandle<T, d>& Rhs) const
	{
		return !(*this == Rhs);
	}

	bool operator<(const TAccelerationStructureHandle<T, d>& Rhs) const
	{
		return HashResult < Rhs.HashResult;
	}

	uint32 GetTypeHash() const
	{
		return HashResult;
	}

	void Serialize(FChaosArchive& Ar);

private:
	TGeometryParticle<T, d>* ExternalGeometryParticle;
	TGeometryParticleHandle<T, d>* GeometryParticleHandle;

	uint32 HashResult;
};

template <typename T, int d>
uint32 GetTypeHash(const TAccelerationStructureHandle<T, d>& A)
{
	return A.GetTypeHash();
}

template <typename T, int d>
class TParticleHandleBase
{
public:
	using TType = T;
	static constexpr int D = d;

	TParticleHandleBase()
		: SerializableGeometryParticles(TSerializablePtr<TGeometryParticles<T,d>>())
		, ParticleIdx(0)
		, Type(EParticleType::Static)
	{
	}

	template <typename TParticlesType>
	TParticleHandleBase(TSerializablePtr<TParticlesType> InParticles, int32 InParticleIdx)
	: SerializableGeometryParticles(InParticles)
	, ParticleIdx(InParticleIdx)
	, Type(InParticles ? InParticles->ParticleType() : EParticleType::Static)
	{
	}

	//Should only be used for transient handles - maybe we can protect this better?
	TParticleHandleBase(TGeometryParticles<T,d>* InParticles, int32 InParticleIdx)
		: SerializableGeometryParticles(TSerializablePtr<TGeometryParticles<T, d>>())
		, ParticleIdx(InParticleIdx)
		, Type(InParticles ? InParticles->ParticleType() : EParticleType::Static)
	{
		GeometryParticles = InParticles;
	}

	//NOTE: this is not virtual and only acceptable because we know the children have no extra data that requires destruction. 
	//You must modify the union to extend this class and do not add any member variables
	~TParticleHandleBase()
	{
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << ParticleIdx;
		uint8 RawType = (uint8)Type;
		Ar << RawType;
		Type = (EParticleType)RawType;
		Ar << SerializableGeometryParticles;
	}

	//This is needed for post serialization fixup of raw pointer. Should only be called by serialization code which is low level and knows the implementation details
	void SetSOALowLevel(TGeometryParticles<T, d>* InParticles)
	{
		//should not be swapping SOAs
		ensure(GeometryParticles == nullptr || GeometryParticles == InParticles);
		GeometryParticles = InParticles;
	}

protected:
	union
	{
		TSerializablePtr<TGeometryParticles<T, d>> SerializableGeometryParticles;
		TGeometryParticles<T, d>* GeometryParticles;
		TKinematicGeometryParticles<T, d>* KinematicGeometryParticles;
		TPBDRigidParticles<T, d>* PBDRigidParticles;
		TPBDRigidClusteredParticles<T, d>* PBDRigidClusteredParticles;
	};

	template <typename TSOA>
	friend class TConstParticleIterator;
	//todo: maybe make private?
	int32 ParticleIdx;	//Index into the particle struct of arrays. Note the index can change
	EParticleType Type;
};

template <typename T, int d, bool bPersistent>
class TKinematicGeometryParticleHandleImp;

template <typename T, int d, bool bPersistent>
class TPBDRigidParticleHandleImp;

template <typename T, int d>
TGeometryParticleHandle<T, d>* GetHandleHelper(TGeometryParticleHandle<T, d>* Handle) { return Handle; }
template <typename T, int d>
const TGeometryParticleHandle<T, d>* GetHandleHelper(const TGeometryParticleHandle<T, d>* Handle) { return Handle; }
template <typename T, int d>
TGeometryParticleHandle<T, d>* GetHandleHelper(TTransientGeometryParticleHandle<T, d>* Handle);
template <typename T, int d>
const TGeometryParticleHandle<T, d>* GetHandleHelper(const TTransientGeometryParticleHandle<T, d>* Handle);

template <typename T, int d>
class TGeometryParticleHandles;

template <typename T, int d, bool bPersistent>
class TGeometryParticleHandleImp : public TParticleHandleBase<T,d>
{
public:
	using TTransientHandle = TTransientGeometryParticleHandle<T, d>;
	using THandleBase = TParticleHandleBase<T, d>;
	using THandleBase::GeometryParticles;
	using THandleBase::ParticleIdx;
	using THandleBase::Type;
	using TSOAType = TGeometryParticles<T,d>;

	static constexpr bool AlwaysSerializable = bPersistent;

	static TGeometryParticleHandleImp<T, d, bPersistent>* SerializationFactory(FChaosArchive& Ar, TGeometryParticleHandleImp<T, d, bPersistent>* Handle);

	template <typename TPayloadType>
	TPayloadType GetPayload(int32 Idx)
	{
		return TPayloadType(Handle());
	}


protected:
	//needed for serialization
	TGeometryParticleHandleImp()
		: TParticleHandleBase<T, d>()
	{
	}

	TGeometryParticleHandleImp(TSerializablePtr<TGeometryParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TGeometryParticleParameters<T, d>& Params)
		: TParticleHandleBase<T, d>(InParticles, InParticleIdx)
		, HandleIdx(InHandleIdx)
	{
		//GeometryParticles->Handle(ParticleIdx) = this;
		//TODO: patch from SOA
		GeometryParticleDefaultConstruct<T, d>(*this, Params);
		SetHasBounds(false);
	}

	template <typename TParticlesType, typename TParams>
	static TUniquePtr<typename TParticlesType::THandleType> CreateParticleHandleHelper(TSerializablePtr<TParticlesType> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TParams& Params)
	{
		check(bPersistent);	//non persistent should not be going through this path
		auto NewHandle = new typename TParticlesType::THandleType(InParticles, InParticleIdx, InHandleIdx, Params);
		TUniquePtr<typename TParticlesType::THandleType> Unique(NewHandle);
		const_cast<TParticlesType*>(InParticles.Get())->SetHandle(InParticleIdx, NewHandle);	//todo: add non const serializable ptr
		return Unique;
	}
public:

	static TUniquePtr<TGeometryParticleHandleImp<T,d, bPersistent>> CreateParticleHandle(TSerializablePtr<TGeometryParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T,d,bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}


	~TGeometryParticleHandleImp()
	{
		if (bPersistent)
		{
			GeometryParticles->DestroyParticle(ParticleIdx);
			if (static_cast<uint32>(ParticleIdx) < GeometryParticles->Size())
			{
				if (GeometryParticles->RemoveParticleBehavior() == ERemoveParticleBehavior::RemoveAtSwap)
				{
					GeometryParticles->Handle(ParticleIdx)->ParticleIdx = ParticleIdx;
				}
				else
				{
					//need to update all handles >= ParticleIdx
					for (int32 Idx = ParticleIdx; static_cast<uint32>(Idx) < GeometryParticles->Size(); ++Idx)
					{
						GeometryParticles->Handle(Idx)->ParticleIdx -= 1;
					}
				}
			}
		}
	}

	template <typename T2, int d2>
	friend TGeometryParticleHandle<T2, d2>* GetHandleHelper(TTransientGeometryParticleHandle<T2, d2>* Handle);
	template <typename T2, int d2>
	friend const TGeometryParticleHandle<T2, d2>* GetHandleHelper(const TTransientGeometryParticleHandle<T2, d2>* Handle);

	TGeometryParticleHandleImp(const TGeometryParticleHandleImp&) = delete;

	const TVector<T, d>& X() const { return GeometryParticles->X(ParticleIdx); }
	TVector<T, d>& X() { return GeometryParticles->X(ParticleIdx); }
	void SetX(const TVector<T, d>& InX) { GeometryParticles->X(ParticleIdx) = InX; }

	const TRotation<T, d>& R() const { return GeometryParticles->R(ParticleIdx); }
	TRotation<T, d>& R() { return GeometryParticles->R(ParticleIdx); }
	void SetR(const TRotation<T, d>& InR) { GeometryParticles->R(ParticleIdx) = InR; }

	TSerializablePtr<FImplicitObject> Geometry() const { return GeometryParticles->Geometry(ParticleIdx); }
	void SetGeometry(TSerializablePtr<FImplicitObject> InGeometry) { GeometryParticles->SetGeometry(ParticleIdx, InGeometry); }

	TSerializablePtr<FImplicitObject> SharedGeometry() const { return GeometryParticles->SharedGeometry(ParticleIdx); }
	void SetSharedGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> InGeometry) { GeometryParticles->SetSharedGeometry(ParticleIdx, InGeometry); }

	const TUniquePtr<FImplicitObject>& DynamicGeometry() const { return GeometryParticles->DynamicGeometry(ParticleIdx); }
	void SetDynamicGeometry(TUniquePtr<FImplicitObject>&& Unique) { GeometryParticles->SetDynamicGeometry(ParticleIdx, MoveTemp(Unique)); }

	const TShapesArray<T,d>& ShapesArray() const { return GeometryParticles->ShapesArray(ParticleIdx); }

	const TBox<T, d>& LocalBounds() const { return GeometryParticles->LocalBounds(ParticleIdx); }
	void SetLocalBounds(const TBox<T, d>& NewBounds) { GeometryParticles->LocalBounds(ParticleIdx) = NewBounds; }

	const TBox<T, d>& WorldSpaceInflatedBounds() const { return GeometryParticles->WorldSpaceInflatedBounds(ParticleIdx); }
	void SetWorldSpaceInflatedBounds(const TBox<T, d>& WorldSpaceInflatedBounds)
	{
		GeometryParticles->SetWorldSpaceInflatedBounds(ParticleIdx, WorldSpaceInflatedBounds);
	}

	bool HasBounds() const { return GeometryParticles->HasBounds(ParticleIdx); }
	void SetHasBounds(bool bHasBounds) { GeometryParticles->HasBounds(ParticleIdx) = bHasBounds; }

	FSpatialAccelerationIdx SpatialIdx() const { return GeometryParticles->SpatialIdx(ParticleIdx); }
	void SetSpatialIdx(FSpatialAccelerationIdx Idx) { GeometryParticles->SpatialIdx(ParticleIdx) = Idx; }

	void SetHashResultLowLevel(uint32 Value) { GeometryParticles->HashResultLowLevel(ParticleIdx) = Value; }
	uint32 GetHashResultLowLevel() const { return GeometryParticles->HashResultLowLevel(ParticleIdx); }

#if CHAOS_CHECKED
	const FName& DebugName() const { return GeometryParticles->DebugName(ParticleIdx); }
	void SetDebugName(const FName& InDebugName) { GeometryParticles->DebugName(ParticleIdx) = InDebugName; }
#endif
	
	EObjectStateType ObjectState() const;

	TGeometryParticle<T, d>* GTGeometryParticle() const { return GeometryParticles->GTGeometryParticle(ParticleIdx); }
	TGeometryParticle<T, d>*& GTGeometryParticle() { return GeometryParticles->GTGeometryParticle(ParticleIdx); }

	const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* CastToKinematicParticle() const;
	TKinematicGeometryParticleHandleImp<T, d, bPersistent>* CastToKinematicParticle();

	const TPBDRigidParticleHandleImp<T, d, bPersistent>* CastToRigidParticle() const;
	TPBDRigidParticleHandleImp<T, d, bPersistent>* CastToRigidParticle();

	const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* CastToClustered() const;
	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* CastToClustered();

	const TGeometryParticleHandle<T, d>* Handle() const { return GetHandleHelper(this);}
	TGeometryParticleHandle<T, d>* Handle() { return GetHandleHelper(this); }

	bool Sleeping() const { return GeometryParticleSleeping(*this); }

	template <typename Container>
	const auto& AuxilaryValue(const Container& AuxContainer) const
	{
		return AuxContainer[HandleIdx];
	}

	template <typename Container>
	auto& AuxilaryValue(Container& AuxContainer)
	{
		return AuxContainer[HandleIdx];
	}

#if CHAOS_DETERMINISTIC
	FParticleID ParticleID() const { return GeometryParticles->ParticleID(ParticleIdx); }
	FParticleID& ParticleID() { return GeometryParticles->ParticleID(ParticleIdx); }
#endif

	void MoveToSOA(TGeometryParticles<T, d>& ToSOA)
	{
		static_assert(bPersistent, "Cannot move particles from a transient handle");
		check(ToSOA.ParticleType() == Type);
		if (GeometryParticles != &ToSOA)
		{
			GeometryParticles->MoveToOtherParticles(ParticleIdx, ToSOA);
			if (static_cast<uint32>(ParticleIdx) < GeometryParticles->Size())
			{
				GeometryParticles->Handle(ParticleIdx)->ParticleIdx = ParticleIdx;
			}
			const int32 NewParticleIdx = ToSOA.Size() - 1;
			ParticleIdx = NewParticleIdx;
			GeometryParticles = &ToSOA;
		}
	}

	static constexpr EParticleType StaticType() { return EParticleType::Static; }

	FString ToString() const;

	void Serialize(FChaosArchive& Ar)
	{
		THandleBase::Serialize(Ar);
		Ar << HandleIdx;
		GeometryParticles->SetHandle(ParticleIdx, this);
	}

protected:

	friend TGeometryParticleHandles<T, d>;
	
	struct FInvalidFromTransient {};
	typename TChooseClass<bPersistent, int32, FInvalidFromTransient>::Result HandleIdx;	//Index into the handles array. This is useful for binding external attributes. Note the index can change
};

template<>
template<>
int32 TGeometryParticleHandleImp<float, 3, true>::GetPayload<int32>(int32 Idx);

template<>
template<>
int32 TGeometryParticleHandleImp<float, 3, false>::GetPayload<int32>(int32 Idx);



template <typename T, int d>
TGeometryParticleHandle<T, d>* GetHandleHelper(TTransientGeometryParticleHandle<T, d>* Handle)
{
	return Handle->GeometryParticles->Handle(Handle->ParticleIdx);
}
template <typename T, int d>
const TGeometryParticleHandle<T, d>* GetHandleHelper(const TTransientGeometryParticleHandle<T, d>* Handle)
{
	return Handle->GeometryParticles->Handle(Handle->ParticleIdx);
}

template <typename T, int d, bool bPersistent>
class TKinematicGeometryParticleHandleImp : public TGeometryParticleHandleImp<T,d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::KinematicGeometryParticles;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TGeometryParticleHandleImp<T, d, bPersistent>::CastToRigidParticle;
	using TTransientHandle = TTransientKinematicGeometryParticleHandle<T, d>;
	using TSOAType = TKinematicGeometryParticles<T, d>;
	
protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;
	//needed for serialization
	TKinematicGeometryParticleHandleImp()
		: TGeometryParticleHandleImp<T, d, bPersistent>()
	{
	}

	TKinematicGeometryParticleHandleImp(TSerializablePtr<TKinematicGeometryParticles<T, d>> Particles, int32 InIdx, int32 InGlobalIdx, const TKinematicGeometryParticleParameters<T, d>& Params)
		: TGeometryParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TGeometryParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, Params);
	}
public:

	static TUniquePtr<TKinematicGeometryParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(TSerializablePtr<TKinematicGeometryParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr <TKinematicGeometryParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TKinematicGeometryParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);	//this is safe because CreateParticleHandle gives back a TUniquePtr
		return Serializable;
	}

	const TVector<T, d>& V() const { return KinematicGeometryParticles->V(ParticleIdx); }
	TVector<T, d>& V() { return KinematicGeometryParticles->V(ParticleIdx); }
	void SetV(const TVector<T, d>& InV) { KinematicGeometryParticles->V(ParticleIdx) = InV; }

	const TVector<T, d>& W() const { return KinematicGeometryParticles->W(ParticleIdx); }
	TVector<T, d>& W() { return KinematicGeometryParticles->W(ParticleIdx); }
	void SetW(const TVector<T, d>& InW) { KinematicGeometryParticles->W(ParticleIdx) = InW; }

	const TKinematicTarget<T, d>& KinematicTarget() const { return KinematicGeometryParticles->KinematicTarget(ParticleIdx); }
	TKinematicTarget<T, d>& KinematicTarget() { return KinematicGeometryParticles->KinematicTarget(ParticleIdx); }

	const TVector<T, d>& CenterOfMass() const { return KinematicGeometryParticles->CenterOfMass(ParticleIdx); }
	void SetCenterOfMass(const TVector<T, d>& InCenterOfMass) { KinematicGeometryParticles->CenterOfMass(ParticleIdx) = InCenterOfMass; }
	
	const TRotation<T, d>& RotationOfMass() const { return KinematicGeometryParticles->RotationOfMass(ParticleIdx); }
	void SetRotationOfMass(const TRotation<T, d>& InRotationOfMass) { KinematicGeometryParticles->RotationOfMass(ParticleIdx) = InRotationOfMass; }

	//Really only useful when using a transient handle
	const TKinematicGeometryParticleHandleImp<T, d, true>* Handle() const { return KinematicGeometryParticles->Handle(ParticleIdx); }
	TKinematicGeometryParticleHandleImp<T, d, true>* Handle() { return KinematicGeometryParticles->Handle(ParticleIdx); }

	EObjectStateType ObjectState() const;
	static constexpr EParticleType StaticType() { return EParticleType::Kinematic; }
};

template <typename T, int d, bool bPersistent>
class TPBDRigidParticleHandleImp : public TKinematicGeometryParticleHandleImp<T, d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::PBDRigidParticles;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TTransientHandle = TTransientPBDRigidParticleHandle<T, d>;
	using TSOAType = TPBDRigidParticles<T, d>;

protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;

	//needed for serialization
	TPBDRigidParticleHandleImp()
		: TKinematicGeometryParticleHandleImp<T, d, bPersistent>()
	{
	}

	TPBDRigidParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TPBDRigidParticles<T, d>> Particles, int32 InIdx, int32 InGlobalIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TKinematicGeometryParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{
		PBDRigidParticleHandleImpDefaultConstruct<T, d>(*this, Params);
		SetIsland(INDEX_NONE);
		SetToBeRemovedOnFracture(false);
	}
public:

	static TUniquePtr<TPBDRigidParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(TSerializablePtr<TPBDRigidParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr <TPBDRigidParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TPBDRigidParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);	//this is safe because CreateParticleHandle gives back a TUniquePtr
		return Serializable;
	}

	operator TPBDRigidParticleHandleImp<T, d, false>& () { return reinterpret_cast<TPBDRigidParticleHandleImp<T, d, false>&>(*this); }

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return PBDRigidParticles->CollisionParticles(ParticleIdx); }
	TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() { return PBDRigidParticles->CollisionParticles(ParticleIdx); }

	int32 CollisionParticlesSize() const { return PBDRigidParticles->CollisionParticlesSize(ParticleIdx); }
	void CollisionParticlesInitIfNeeded() { PBDRigidParticles->CollisionParticlesInitIfNeeded(ParticleIdx); }
	void SetCollisionParticles(TParticles<T, d>&& Points) { PBDRigidParticles->SetCollisionParticles(ParticleIdx, MoveTemp(Points)); }

	int32 CollisionGroup() const { return PBDRigidParticles->CollisionGroup(ParticleIdx); }
	int32& CollisionGroup() { return PBDRigidParticles->CollisionGroup(ParticleIdx); }
	void SetCollisionGroup(const int32 InCollisionGroup) { PBDRigidParticles->CollisionGroup(ParticleIdx) = InCollisionGroup; }

	bool Disabled() const { return PBDRigidParticles->Disabled(ParticleIdx); }
	bool& Disabled() { return PBDRigidParticles->DisabledRef(ParticleIdx); }

	// See Comment on TRigidParticle::SetDisabledLowLevel. State changes in Evolution should accompany this call.
	void SetDisabledLowLevel(bool disabled) { PBDRigidParticles->SetDisabledLowLevel(ParticleIdx, disabled); }
	void SetDisabled(const bool InDisabled) { PBDRigidParticles->DisabledRef(ParticleIdx) = InDisabled; }

	const TVector<T, d>& PreV() const { return PBDRigidParticles->PreV(ParticleIdx); }
	TVector<T, d>& PreV() { return PBDRigidParticles->PreV(ParticleIdx); }
	void SetPreV(const TVector<T, d>& InPreV) { PBDRigidParticles->PreV(ParticleIdx) = InPreV; }

	const TVector<T, d>& PreW() const { return PBDRigidParticles->PreW(ParticleIdx); }
	TVector<T, d>& PreW() { return PBDRigidParticles->PreW(ParticleIdx); }
	void SetPreW(const TVector<T, d>& InPreW) { PBDRigidParticles->PreW(ParticleIdx) = InPreW; }

	const TVector<T, d>& P() const { return PBDRigidParticles->P(ParticleIdx); }
	TVector<T, d>& P() { return PBDRigidParticles->P(ParticleIdx); }
	void SetP(const TVector<T, d>& InP) { PBDRigidParticles->P(ParticleIdx) = InP; }

	const TRotation<T, d>& Q() const { return PBDRigidParticles->Q(ParticleIdx); }
	TRotation<T, d>& Q() { return PBDRigidParticles->Q(ParticleIdx); }
	void SetQ(const TRotation<T, d>& InQ) { PBDRigidParticles->Q(ParticleIdx) = InQ; }

	const TVector<T, d>& F() const { return PBDRigidParticles->F(ParticleIdx); }
	TVector<T, d>& F() { return PBDRigidParticles->F(ParticleIdx); }
	void SetF(const TVector<T, d>& InF) { PBDRigidParticles->F(ParticleIdx) = InF; }

	const TVector<T, d>& Torque() const { return PBDRigidParticles->Torque(ParticleIdx); }
	TVector<T, d>& Torque() { return PBDRigidParticles->Torque(ParticleIdx); }
	void SetTorque(const TVector<T, d>& InTorque) { PBDRigidParticles->Torque(ParticleIdx) = InTorque; }

	const TVector<T, d>& ExternalForce() const { return PBDRigidParticles->ExternalForce(ParticleIdx); }
	TVector<T, d>& ExternalForce() { return PBDRigidParticles->ExternalForce(ParticleIdx); }
	void SetExternalForce(const TVector<T, d>& InF) { PBDRigidParticles->ExternalForce(ParticleIdx) = InF; }

	const TVector<T, d>& ExternalTorque() const { return PBDRigidParticles->ExternalTorque(ParticleIdx); }
	TVector<T, d>& ExternalTorque() { return PBDRigidParticles->ExternalTorque(ParticleIdx); }
	void SetExternalTorque(const TVector<T, d>& InTorque) { PBDRigidParticles->ExternalTorque(ParticleIdx) = InTorque; }

	const PMatrix<T, d, d>& I() const { return PBDRigidParticles->I(ParticleIdx); }
	PMatrix<T, d, d>& I() { return PBDRigidParticles->I(ParticleIdx); }
	void SetI(const PMatrix<T, d, d>& InI) { PBDRigidParticles->I(ParticleIdx) = InI; }

	const PMatrix<T, d, d>& InvI() const { return PBDRigidParticles->InvI(ParticleIdx); }
	PMatrix<T, d, d>& InvI() { return PBDRigidParticles->InvI(ParticleIdx); }
	void SetInvI(const PMatrix<T, d, d>& InInvI) { PBDRigidParticles->InvI(ParticleIdx) = InInvI; }

	T M() const { return PBDRigidParticles->M(ParticleIdx); }
	T& M() { return PBDRigidParticles->M(ParticleIdx); }
	void SetM(const T& InM) { PBDRigidParticles->M(ParticleIdx) = InM; }

	T InvM() const { return PBDRigidParticles->InvM(ParticleIdx); }
	T& InvM() { return PBDRigidParticles->InvM(ParticleIdx); }
	void SetInvM(const T& InInvM) { PBDRigidParticles->InvM(ParticleIdx) = InInvM; }

	int32 Island() const { return PBDRigidParticles->Island(ParticleIdx); }
	int32& Island() { return PBDRigidParticles->Island(ParticleIdx); }
	void SetIsland(const int32 InIsland) { PBDRigidParticles->Island(ParticleIdx) = InIsland; }

	bool ToBeRemovedOnFracture() const { return PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx); }
	bool& ToBeRemovedOnFracture() { return PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx); }
	void SetToBeRemovedOnFracture(const bool bToBeRemovedOnFracture) { PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx) = bToBeRemovedOnFracture; }

	EObjectStateType ObjectState() const { return PBDRigidParticles->ObjectState(ParticleIdx); }
	void SetObjectStateLowLevel(EObjectStateType InState) { PBDRigidParticles->SetObjectState(ParticleIdx, InState); }
	void SetSleeping(bool bSleeping) { PBDRigidParticles->SetSleeping(ParticleIdx, bSleeping); }

	//Really only useful when using a transient handle
	const TPBDRigidParticleHandleImp<T, d, true>* Handle() const { return PBDRigidParticles->Handle(ParticleIdx); }
	TPBDRigidParticleHandleImp<T, d, true>* Handle() { return PBDRigidParticles->Handle(ParticleIdx); }

	static constexpr EParticleType StaticType() { return EParticleType::Rigid; }
};

template <typename T, int d, bool bPersistent>
class TPBDRigidClusteredParticleHandleImp : public TPBDRigidParticleHandleImp<T, d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::PBDRigidClusteredParticles;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TTransientHandle = TTransientPBDRigidParticleHandle<T, d>;
	using TSOAType = TPBDRigidClusteredParticles<T, d>;

protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;
	//needed for serialization
	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>()
	: TPBDRigidParticleHandleImp<T, d, bPersistent>()
	{
	}

	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TPBDRigidClusteredParticles<T, d>> Particles, int32 InIdx, int32 InGlobalIdx, const TPBDRigidParticleParameters<T, d>& Params)
		: TPBDRigidParticleHandleImp<T, d, bPersistent>(TSerializablePtr<TPBDRigidParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{
		PBDRigidClusteredParticleDefaultConstruct<T, d>(*this, Params);
	}
public:

	static TUniquePtr<TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(TSerializablePtr<TPBDRigidClusteredParticles<T, d>> InParticles, int32 InParticleIdx, int32 InHandleIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr <TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);	//this is safe because CreateParticleHandle gives back a TUniquePtr
		return Serializable;
	}

	const auto& ClusterIds() const { return PBDRigidClusteredParticles->ClusterIds(ParticleIdx); }
	auto& ClusterIds() { return PBDRigidClusteredParticles->ClusterIds(ParticleIdx); }

	const auto& ChildToParent() const { return PBDRigidClusteredParticles->ChildToParent(ParticleIdx); }
	auto& ChildToParent() { return PBDRigidClusteredParticles->ChildToParent(ParticleIdx); }

	const auto& ClusterGroupIndex() const { return PBDRigidClusteredParticles->ClusterGroupIndex(ParticleIdx); }
	auto& ClusterGroupIndex() { return PBDRigidClusteredParticles->ClusterGroupIndex(ParticleIdx); }

	const auto& InternalCluster() const { return PBDRigidClusteredParticles->InternalCluster(ParticleIdx); }
	auto& InternalCluster() { return PBDRigidClusteredParticles->InternalCluster(ParticleIdx); }

	const auto& ChildrenSpatial() const { return PBDRigidClusteredParticles->ChildrenSpatial(ParticleIdx); }
	auto& ChildrenSpatial() { return PBDRigidClusteredParticles->ChildrenSpatial(ParticleIdx); }

	//const auto& MultiChildProxyId() const { return PBDRigidClusteredParticles->MultiChildProxyId(ParticleIdx); }
	//auto& MultiChildProxyId() { return PBDRigidClusteredParticles->MultiChildProxyId(ParticleIdx); }

	//const auto& MultiChildProxyData() const { return PBDRigidClusteredParticles->MultiChildProxyData(ParticleIdx); }
	//auto& MultiChildProxyData() { return PBDRigidClusteredParticles->MultiChildProxyData(ParticleIdx); }

	const auto& CollisionImpulses() const { return PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx); }
	auto& CollisionImpulses() { return PBDRigidClusteredParticles->CollisionImpulses(ParticleIdx); }

	const auto& Strains() const { return PBDRigidClusteredParticles->Strains(ParticleIdx); }
	auto& Strains() { return PBDRigidClusteredParticles->Strains(ParticleIdx); }

	const TPBDRigidClusteredParticleHandleImp<T, d, true>* Handle() const { return PBDRigidClusteredParticles->Handle(ParticleIdx); }
	TPBDRigidClusteredParticleHandleImp<T, d, true>* Handle() { return PBDRigidClusteredParticles->Handle(ParticleIdx); }

	static constexpr EParticleType StaticType() { return EParticleType::Rigid; }

	int32 TransientParticleIndex() const { return ParticleIdx; }
};

template <typename T, int d, bool bPersistent = true>
class TPBDGeometryCollectionParticleHandleImp : public TPBDRigidParticleHandleImp<T, d, bPersistent>
{
public:
	using TGeometryParticleHandleImp<T, d, bPersistent>::ParticleIdx;
	using TGeometryParticleHandleImp<T, d, bPersistent>::PBDRigidParticles;
	using TGeometryParticleHandleImp<T, d, bPersistent>::Type;
	using TTransientHandle = TTransientPBDGeometryCollectionParticleHandle<T, d>;
	using TSOAType = TPBDGeometryCollectionParticles<T, d>;

protected:
	friend class TGeometryParticleHandleImp<T, d, bPersistent>;

	//needed for serialization
	TPBDGeometryCollectionParticleHandleImp()
		: TPBDRigidParticleHandleImp<T, d, bPersistent>()
	{}

	TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>(
		TSerializablePtr<TPBDGeometryCollectionParticles<T, d>> Particles, 
		int32 InIdx, 
		int32 InGlobalIdx, 
		const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
		: TPBDRigidParticleHandleImp<T, d, bPersistent>(
			TSerializablePtr<TPBDRigidParticles<T, d>>(Particles), InIdx, InGlobalIdx, Params)
	{}
public:

	static TUniquePtr<TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>> CreateParticleHandle(
		TSerializablePtr<TPBDGeometryCollectionParticles<T, d>> InParticles, 
		int32 InParticleIdx, 
		int32 InHandleIdx, 
		const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		return TGeometryParticleHandleImp<T, d, bPersistent>::CreateParticleHandleHelper(
			InParticles, InParticleIdx, InHandleIdx, Params);
	}

	TSerializablePtr<TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>> ToSerializable() const
	{
		TSerializablePtr<TPBDGeometryCollectionParticleHandleImp<T, d, bPersistent>> Serializable;
		Serializable.SetFromRawLowLevel(this);
		return Serializable;
	}

	const TPBDGeometryCollectionParticleHandleImp<T, d, true>* Handle() const { return PBDRigidParticles->Handle(ParticleIdx); }
	TPBDGeometryCollectionParticleHandleImp<T, d, true>* Handle() { return PBDRigidParticles->Handle(ParticleIdx); }

	static constexpr EParticleType StaticType() { return EParticleType::GeometryCollection; }
};

template <typename T, int d, bool bPersistent>
TKinematicGeometryParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToKinematicParticle() { return Type >= EParticleType::Kinematic ? static_cast<TKinematicGeometryParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T,d, bPersistent>::CastToKinematicParticle() const { return Type >= EParticleType::Kinematic ? static_cast<const TKinematicGeometryParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TPBDRigidParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToRigidParticle() const { return Type >= EParticleType::Rigid ? static_cast<const TPBDRigidParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
TPBDRigidParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToRigidParticle() { return Type >= EParticleType::Rigid ? static_cast<TPBDRigidParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToClustered() const { return Type >= EParticleType::Clustered ? static_cast<const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::CastToClustered() { return Type >= EParticleType::Clustered ? static_cast<TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
EObjectStateType TGeometryParticleHandleImp<T,d, bPersistent>::ObjectState() const
{
	const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* Kin = CastToKinematicParticle();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d, bool bPersistent>
EObjectStateType TKinematicGeometryParticleHandleImp<T, d, bPersistent>::ObjectState() const
{
	const TPBDRigidParticleHandleImp<T, d, bPersistent>* Dyn = CastToRigidParticle();
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}

template <typename T, int d, bool bPersistent>
FString TGeometryParticleHandleImp<T, d, bPersistent>::ToString() const
{
	switch (Type)
	{
	case EParticleType::Static:
		return FString::Printf(TEXT("Static[%d]"), ParticleIdx);
		break;
	case EParticleType::Kinematic:
		return FString::Printf(TEXT("Kinemmatic[%d]"), ParticleIdx);
		break;
	case EParticleType::Rigid:
		return FString::Printf(TEXT("Dynamic[%d]"), ParticleIdx);
	case EParticleType::GeometryCollection:
		return FString::Printf(TEXT("GeometryCollection[%d]"), ParticleIdx);
		break;
	}
	return FString();
}

template <typename T, int d, bool bPersistent>
TGeometryParticleHandleImp<T,d,bPersistent>* TGeometryParticleHandleImp<T,d, bPersistent>::SerializationFactory(FChaosArchive& Ar, TGeometryParticleHandleImp<T, d, bPersistent>* Handle)
{
	check(bPersistent);
	static_assert(sizeof(TGeometryParticleHandleImp<T, d, bPersistent>) == sizeof(TGeometryParticleHandleImp<T, d, bPersistent>), "No new members in derived classes");
	return Ar.IsLoading() ? new TGeometryParticleHandleImp<T, d, bPersistent>() : nullptr;
}

/**
 * A wrapper around any type of particle handle to provide a consistent (read-only) API for all particle types.
 * This can make code simpler because you can write code that is type-agnostic, but it
 * has a cost. Where possible it is better to write code that is specific to the type(s)
 * of particles being operated on. TGenericParticleHandle has pointer symantics, so you can use one wherever
 * you have a particle handle pointer;
 *
 */
template <typename T, int d>
class TGenericParticleHandle
{
public:
	TGenericParticleHandle(TGeometryParticleHandle<T, d>* InHandle) : Imp(InHandle) {}

	class HandleImp
	{
	public:
		HandleImp(TGeometryParticleHandle<T, d>* InHandle) { Handle = InHandle; }

		// Check for the exact type of particle (see also AsKinematic etc, which will work on derived types)
		bool IsStatic() const { return (Handle->ObjectState() == EObjectStateType::Static); }
		bool IsKinematic() const { return (Handle->ObjectState() == EObjectStateType::Kinematic); }
		bool IsDynamic() const { return (Handle->ObjectState() == EObjectStateType::Dynamic) || (Handle->ObjectState() == EObjectStateType::Sleeping); }

		const TKinematicGeometryParticleHandle<T, d>* CastToKinematicParticle() const { return Handle->CastToKinematicParticle(); }
		TKinematicGeometryParticleHandle<T, d>* CastToKinematicParticle() { return Handle->CastToKinematicParticle(); }
		const TPBDRigidParticleHandle<T, d>* CastToRigidParticle() const { return Handle->CastToRigidParticle(); }
		TPBDRigidParticleHandle<T, d>* CastToRigidParticle() { return Handle->CastToRigidParticle(); }

		// Static Particles
		const TVector<T, d>& X() const { return Handle->X(); }
		const TRotation<T, d>& R() const { return Handle->R(); }
		TSerializablePtr<FImplicitObject> Geometry() const { return Handle->Geometry(); }
		const TUniquePtr<FImplicitObject>& DynamicGeometry() const { return Handle->DynamicGeometry(); }
		bool Sleeping() const { return Handle->Sleeping(); }
		FString ToString() const { return Handle->ToString(); }

		template <typename Container>
		const auto& AuxilaryValue(const Container& AuxContainer) const { return Handle->AuxilaryValue(AuxContainer); }
		template <typename Container>
		auto& AuxilaryValue(Container& AuxContainer) { return Handle->AuxilaryValue(AuxContainer); }

		// Kinematic Particles
		const TVector<T, d>& V() const { return (Handle->CastToKinematicParticle()) ? Handle->CastToKinematicParticle()->V() : ZeroVector; }
		const TVector<T, d>& W() const { return (Handle->CastToKinematicParticle()) ? Handle->CastToKinematicParticle()->W() : ZeroVector; }

		// Dynamic Particles

		// TODO: Make all of these check ObjectState to maintain current functionality
		int32 CollisionParticlesSize() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->CollisionParticlesSize();
			}

			return 0;
		}

		const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->CollisionParticles();
			}

			return NullBVHParticles;
		}

		int32 CollisionGroup() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->CollisionGroup();
			}

			return 0;
		}

		// @todo(ccaulfield): should be available on all types?
		bool Disabled() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->Disabled();
			}

			return false;
		}

		// @todo(ccaulfield): should be available on kinematics?
		const TVector<T, d>& PreV() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->PreV();
			}

			return ZeroVector;
		}

		// @todo(ccaulfield): should be available on kinematics?
		const TVector<T, d>& PreW() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->PreW();
			}
			return ZeroVector;
		}

		const TVector<T, d>& P() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->P();
			}

			return X();
		}

		const TRotation<T, d>& Q() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->Q();
			}

			return R();
		}

		const TVector<T, d>& F() const 
		{ 
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->F();
			}

			return ZeroVector;
		}
		const TVector<T, d>& Torque() const 
		{ 
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->Torque();
			}

			return ZeroVector;
		}

		const PMatrix<T, d, d>& I() const 
		{ 
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->I();
			}

			return ZeroMatrix;
		}

		const PMatrix<T, d, d>& InvI() const 
		{ 
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->InvI();
			}

			return ZeroMatrix;
		}

		T M() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->M();
			}

			return (T)0;
		}

		T InvM() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->InvM();
			}

			return (T)0;
		}

		int32 Island() const
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->Island();
			}

			return INDEX_NONE;
		}

		bool ToBeRemovedOnFracture() const 
		{
			if (Handle->CastToRigidParticle() && Handle->ObjectState() == EObjectStateType::Dynamic)
			{
				return Handle->CastToRigidParticle()->ToBeRemovedOnFracture();
			}

			return false;
		}

	private:
		TGeometryParticleHandle<T, d>* Handle;
	};

	HandleImp* operator->() { return &Imp; }
	const HandleImp* operator->() const { return &Imp; }

private:
	HandleImp Imp;

	static const TVector<T, d> ZeroVector;
	static const TRotation<T, d> IdentityRotation;
	static const PMatrix<T, d, d> ZeroMatrix;
	static const TUniquePtr<TBVHParticles<T, d>> NullBVHParticles;
};

template <typename T, int d>
const TVector<T, d> TGenericParticleHandle<T, d>::ZeroVector = TVector<T, d>(0);

template <typename T, int d>
const TRotation<T, d> TGenericParticleHandle<T, d>::IdentityRotation = TRotation<T, d>(0, 0, 0, 1);

template <typename T, int d>
const PMatrix<T, d, d> TGenericParticleHandle<T, d>::ZeroMatrix = PMatrix<T, d, d>(0);

template <typename T, int d>
const TUniquePtr<TBVHParticles<T, d>> TGenericParticleHandle<T, d>::NullBVHParticles = TUniquePtr<TBVHParticles<T, d>>();

template <typename T, int d>
class TGeometryParticleHandles : public TArrayCollection
{
public:
	TGeometryParticleHandles()
	{
		AddArray(&Handles);
	}

	void AddHandles(int32 NumHandles)
	{
		AddElementsHelper(NumHandles);
	}

	void Reset()
	{
		ResizeHelper(0);
	}

	void DestroyHandleSwap(TGeometryParticleHandle<T,d>* Handle)
	{
		const int32 UnstableIdx = Handle->HandleIdx;
		RemoveAtSwapHelper(UnstableIdx);
		if (static_cast<uint32>(UnstableIdx) < Size())
		{
			Handles[UnstableIdx]->HandleIdx = UnstableIdx;
		}
	}

	void Serialize(FChaosArchive& Ar)
	{
		Ar << Handles;
		ResizeHelper(Handles.Num());
	}

	const TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) const { return Handles[Idx];}
	TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) { return Handles[Idx]; }
private:
	TArrayCollectionArray<TUniquePtr<TGeometryParticleHandleImp<T, d, true>>> Handles;
};

template <typename T, int d>
class TKinematicGeometryParticle;

template <typename T, int d>
class TPBDRigidParticle;


/**
 * Base class for transient classes used to communicate simulated particle state 
 * between game and physics threads, which is managed by proxies.
 *
 * Note the lack of virtual api.
 */
class FParticleData
{
public:
	FParticleData(EParticleType InType = EParticleType::Static)
		: Type(InType)
	{}

	void Reset() { Type = EParticleType::Static; }

	EParticleType Type;
};

template <typename T, int d> 
class TGeometryParticleData;

template <typename T, int d>
class TKinematicGeometryParticleData;

template <typename T, int d>
class TPBDRigidParticleData;


template <typename T, int d>
class TGeometryParticle
{
public:
	typedef TGeometryParticleData<T, d> FData;
	typedef TGeometryParticleHandle<T, d> FHandle;

	friend FData;

	static constexpr bool AlwaysSerializable = true;

protected:
	TGeometryParticle(const TGeometryParticleParameters<T, d>& StaticParams = TGeometryParticleParameters<T, d>())
		: MUserData(nullptr)
	{
		Type = EParticleType::Static;
		Proxy = nullptr;
		GeometryParticleDefaultConstruct<T, d>(*this, StaticParams);
	}

public:
	static TUniquePtr<TGeometryParticle<T, d>> CreateParticle(const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
	{
		return TUniquePtr< TGeometryParticle<T, d>>(new TGeometryParticle<T, d>(Params));
	}

	virtual ~TGeometryParticle() {}	//only virtual for easier memory management. Should generally be a static API

	TGeometryParticle(const TGeometryParticle&) = delete;

	TGeometryParticle& operator=(const TGeometryParticle&) = delete;

	virtual void Serialize(FChaosArchive& Ar)
	{
		Ar << MX;
		Ar << MR;
		Ar << MGeometry;
		Ar << MShapesArray;
		Ar << Type;
		//Ar << MDirtyFlags;

		Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
		if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::SerializeShapeWorldSpaceBounds)
		{
			if (MGeometry->HasBoundingBox())
			{
				for (auto& Shape : MShapesArray)
				{
					Shape->UpdateShapeBounds(FRigidTransform3(MX, MR));
				}
			}
		}
	}

	virtual bool IsParticleValid() const
	{
		return MGeometry && MGeometry->IsValidGeometry();	//todo: if we want support for sample particles without geometry we need to adjust this
	}

	static TGeometryParticle<T, d>* SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable);

	const TVector<T, d>& X() const { return this->MX; }
	void SetX(const TVector<T, d>& InX, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::X, bInvalidate);
		this->MX = InX;
	}

	const TRotation<T, d>& R() const { return this->MR; }
	void SetR(const TRotation<T, d>& InR, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::R, bInvalidate);
		this->MR = InR;
	}

	//todo: geometry should not be owned by particle
	void SetGeometry(TUniquePtr<FImplicitObject>&& UniqueGeometry)
	{
		// Take ownership of the geometry, putting it into a shared ptr.
		// This is necessary because we cannot be sure whether the particle
		// will be destroyed on the game thread or physics thread first,
		// but geometry data is shared between them.
		FImplicitObject* RawGeometry = UniqueGeometry.Release();
		SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe>(RawGeometry));
	}

	// TODO: Right now this method exists so we can do things like FPhysTestSerializer::CreateChaosData.
	//       We should replace this with a method for supporting SetGeometry(RawGeometry).
	void SetGeometry(TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> SharedGeometry)
	{
		this->MarkDirty(EParticleFlags::Geometry);
		this->MGeometry = SharedGeometry;
		UpdateShapesArray();
	}

	void SetGeometry(TSerializablePtr<FImplicitObject> RawGeometry)
	{
		// Ultimately this method should replace SetGeometry(SharedPtr).
		// We don't really want people making shared ptrs to geometry everywhere.
		check(false);
	}

	void* UserData() const { return this->MUserData; }
	void SetUserData(void* InUserData)
	{
		this->MUserData = InUserData;
	}

#if CHAOS_CHECKED
	const FName& DebugName() const { return this->MDebugName; }
	void SetDebugName(const FName& InDebugName)
	{
		this->MarkDirty(EParticleFlags::DebugName);
		this->MDebugName = InDebugName;
	}
#endif

	void SetHashResultLowLevel(uint32 Value)
	{
		MarkDirty(EParticleFlags::HashResult);
		MHashResult = Value;
	}

	uint32 GetHashResultLowLevel() const { return MHashResult; }

	//Note: this must be called after setting geometry. This API seems bad. Should probably be part of setting geometry
	void SetShapesArray(TShapesArray<T, d>&& InShapesArray)
	{
		ensure(InShapesArray.Num() == MShapesArray.Num());
		MShapesArray = MoveTemp(InShapesArray);
	}

	TSerializablePtr<FImplicitObject> Geometry() const { return MakeSerializable(MGeometry); }

	const TShapesArray<T,d>& ShapesArray() const { return MShapesArray; }

	EObjectStateType ObjectState() const;

	EParticleType ObjectType() const
	{
		return Type;
	}


	const TKinematicGeometryParticle<T, d>* CastToKinematicParticle() const;
	TKinematicGeometryParticle<T, d>* CastToKinematicParticle();

	const TPBDRigidParticle<T, d>* CastToRigidParticle() const;
	TPBDRigidParticle<T, d>* CastToRigidParticle();

	FSpatialAccelerationIdx SpatialIdx() const { return MSpatialIdx; }
	void SetSpatialIdx(FSpatialAccelerationIdx Idx)
	{
		MarkDirty(EParticleFlags::SpatialIdx);
		MSpatialIdx = Idx;
	}

	FParticleData* NewData() const { return new TGeometryParticleData<T, d>( *this ); }

	bool IsDirty() const
	{
		return this->MDirtyFlags.IsDirty();
	}

	bool IsDirty(const EParticleFlags CheckBits) const
	{
		return this->MDirtyFlags.IsDirty(CheckBits);
	}

	const FParticleDirtyFlags& DirtyFlags() const
	{
		return MDirtyFlags;
	}

	void ClearDirtyFlags()
	{
		this->MDirtyFlags.Clear();
	}

	TGeometryParticleHandle<T, d>* Handle() const
	{
		if (Proxy)
		{
			return static_cast<TGeometryParticleHandle<T, d>*>(Proxy->GetHandleUnsafe());
		}

		return nullptr;
	}

	// Pointer to any data that the solver wants to associate with this particle
	// TODO: It's important to eventually hide this!
	// Right now it's exposed to lubricate the creation of the whole proxy system.
	class IPhysicsProxyBase* Proxy;

private:
	TVector<T, d> MX;
	TRotation<T, d> MR;
	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> MGeometry;	//TODO: geometry should live in bodysetup
	TShapesArray<T,d> MShapesArray;
	FSpatialAccelerationIdx MSpatialIdx;

	// This value is generated and used in AccelerationStructureHandle for a hash, as that handle
	// sometimes only has a particle pointer from game thread or phys thread, no easy way to get stable hash from that.
	uint32 MHashResult;

	// Pointer to some arbitrary data associated with the particle, but not used by Chaos. External systems may use this for whatever.
	void* MUserData;

	// This is only for use by ParticleData. This should be called only in one place,
	// when the geometry is being copied from GT to PT.
	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> GeometrySharedLowLevel() const
	{
		return MGeometry;
	}

#if CHAOS_CHECKED
	FName MDebugName;
#endif

protected:
	EParticleType Type;
	FParticleDirtyFlags MDirtyFlags;

	void MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate = true);

	void UpdateShapesArray()
	{
		UpdateShapesArrayFromGeometry<T, d>(MShapesArray, MakeSerializable(MGeometry), FRigidTransform3(X(), R()));
	}
};

template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TGeometryParticle<T, d>& Particle)
{
	Particle.Serialize(Ar);
	return Ar;
}

template <typename T, int d>
class TGeometryParticleData : public FParticleData
{
	typedef FParticleData Base;
public:

	TGeometryParticleData(EParticleType InType = EParticleType::Static)
		: FParticleData(InType)
		, X(TVector<T, d>(0))
		, R(TRotation<T, d>())
		, SpatialIdx(FSpatialAccelerationIdx{ 0,0 })
		, HashResult(0)
		, DirtyFlags()
#if CHAOS_CHECKED
		, DebugName(NAME_None)
#endif
	{}

	TGeometryParticleData(const TGeometryParticle<T, d>& InParticle)
		: FParticleData(EParticleType::Static)
		, X(InParticle.X())
		, R(InParticle.R())
		, Geometry(InParticle.GeometrySharedLowLevel())
		, SpatialIdx(InParticle.SpatialIdx())
		, HashResult(InParticle.GetHashResultLowLevel())
		, DirtyFlags(InParticle.DirtyFlags())
#if CHAOS_CHECKED
		, DebugName(InParticle.DebugName())
#endif
	{}

	void Reset() 
	{ 
		FParticleData::Reset();  
		X = TVector<T, d>(0); 
		R = TRotation<T, d>(); 
		Geometry = TSharedPtr<TImplicitObjectUnion<T, d>, ESPMode::ThreadSafe>();
		SpatialIdx = FSpatialAccelerationIdx{ 0,0 };
		HashResult = 0;
		DirtyFlags.Clear();
#if CHAOS_CHECKED
		DebugName = NAME_None;
#endif
	}

	TVector<T, d> X;
	TRotation<T, d> R;
	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> Geometry;
	FSpatialAccelerationIdx SpatialIdx;
	uint32 HashResult;
	FParticleDirtyFlags DirtyFlags;
#if CHAOS_CHECKED
	FName DebugName;
#endif
};


template <typename T, int d>
class TKinematicGeometryParticle : public TGeometryParticle<T, d>
{
public:
	typedef TKinematicGeometryParticleData<T, d> FData;
	typedef TKinematicGeometryParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;
	using TGeometryParticle<T, d>::CastToRigidParticle;

protected:
	friend TGeometryParticle<T,d>* TGeometryParticle<T, d>::SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable);
	TKinematicGeometryParticle(const TKinematicGeometryParticleParameters<T, d>& KinematicParams = TKinematicGeometryParticleParameters<T,d>())
		: TGeometryParticle<T, d>(KinematicParams)
	{
		Type = EParticleType::Kinematic;
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, KinematicParams);
	}
public:
	static TUniquePtr<TKinematicGeometryParticle<T, d>> CreateParticle(const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>())
	{
		return TUniquePtr< TKinematicGeometryParticle<T, d>>(new TKinematicGeometryParticle<T, d>(Params));
	}

	virtual void Serialize(FChaosArchive& Ar) override
	{
		TGeometryParticle<T, d>::Serialize(Ar);
		Ar << MV;
		Ar << MW;
	}

	const TVector<T, d>& V() const { return MV; }
	void SetV(const TVector<T, d>& InV, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::V, bInvalidate);
		this->MV = InV;
	}

	const TVector<T, d>& W() const { return MW; }
	void SetW(const TVector<T, d>& InW, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::W, bInvalidate);
		this->MW = InW;
	}

	const TVector<T, d>& CenterOfMass() const { return MCenterOfMass; }
	void SetCenterOfMass(const TVector<T, d>& InCenterOfMass, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::CenterOfMass, bInvalidate);
		this->MCenterOfMass = InCenterOfMass;
	}
	
	const TRotation<T, d>& RotationOfMass() const { return MRotationOfMass; }
	void SetRotationOfMass(const TRotation<T, d>& InRotationOfMass, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::RotationOfMass, bInvalidate);
		this->MRotationOfMass = InRotationOfMass;
	}

	EObjectStateType ObjectState() const;

	FParticleData* NewData() const
	{
		return new TKinematicGeometryParticleData<T, d>(*this);
	}

private:
	TVector<T, d> MV;
	TVector<T, d> MW;
	TVector<T, d> MCenterOfMass;
	TRotation<T, d> MRotationOfMass;
};

template <typename T, int d>
class TKinematicGeometryParticleData : public TGeometryParticleData<T, d>
{
	typedef TGeometryParticleData<T, d> Base;
public:

	using TGeometryParticleData<T, d>::Type;

	TKinematicGeometryParticleData(EParticleType InType = EParticleType::Kinematic)
		: Base(InType)
		, MV(TVector<T, d>(0))
		, MW(TVector<T, d>(0))
		, MCenterOfMass(TVector<T, d>(0))
		, MRotationOfMass(TRotation<T, d>(FQuat(EForceInit::ForceInit))) {}

	TKinematicGeometryParticleData(const TKinematicGeometryParticle<T, d>& InParticle)
		: Base(InParticle)
		, MV(InParticle.V())
		, MW(InParticle.W()) 
		, MCenterOfMass(InParticle.CenterOfMass()) 
		, MRotationOfMass(InParticle.RotationOfMass()) 
	{
		Type = EParticleType::Kinematic;
	}


	void Reset() {
		TGeometryParticleData<T, d>::Reset();
		Type = EParticleType::Kinematic;
		MV = TVector<T, d>(0);
		MW = TVector<T, d>(0);
		MCenterOfMass = TVector<T, d>(0);
		MRotationOfMass = TRotation<T, d>(FQuat(EForceInit::ForceInit));
	}

	TVector<T, d> MV;
	TVector<T, d> MW;
	TVector<T, d> MCenterOfMass;
	TRotation<T, d> MRotationOfMass;
};


template <typename T, int d>
class TPBDRigidParticle : public TKinematicGeometryParticle<T, d>
{
public:
	typedef TPBDRigidParticleData<T, d> FData;
	typedef TPBDRigidParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;

protected:
	friend TGeometryParticle<T, d>* TGeometryParticle<T, d>::SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable);
	TPBDRigidParticle<T, d>(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticle<T, d>(DynamicParams)
	{
		Type = EParticleType::Rigid;
		MIsland = INDEX_NONE;
		MToBeRemovedOnFracture = false;
		PBDRigidParticleDefaultConstruct<T, d>(*this, DynamicParams);
	}
public:

	static TUniquePtr<TPBDRigidParticle<T, d>> CreateParticle(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
	{
		return TUniquePtr< TPBDRigidParticle<T, d>>(new TPBDRigidParticle<T, d>(DynamicParams));
	}

	void Serialize(FChaosArchive& Ar) override
	{
		TKinematicGeometryParticle<T, d>::Serialize(Ar);
		Ar << MQ;
		Ar << MPreV;
		Ar << MPreW;
		Ar << MP;
		Ar << MF;
		Ar << MTorque;
		Ar << MExternalForce;
		Ar << MExternalTorque;
		Ar << MI;
		Ar << MInvI;
		Ar << MCollisionParticles;
		Ar << MM;
		Ar << MInvM;
		Ar << MIsland;
		Ar << MCollisionGroup;
		Ar << MObjectState;
		Ar << MDisabled;
		Ar << MToBeRemovedOnFracture;
		Ar << MGravityEnabled;
	}

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return MCollisionParticles; }

	int32 CollisionGroup() const { return MCollisionGroup; }
	void SetCollisionGroup(const int32 InCollisionGroup)
	{
		this->MarkDirty(EParticleFlags::CollisionGroup);
		this->MCollisionGroup = InCollisionGroup;
	}

	bool Disabled() const { return MDisabled; }
	void SetDisabled(const bool InDisabled)
	{
		this->MarkDirty(EParticleFlags::CollisionGroup);
		this->MDisabled = InDisabled;
	}

	bool IsGravityEnabled() const { return MGravityEnabled; }
	void SetGravityEnabled(const bool InGravityEnabled)
	{
		this->MarkDirty(EParticleFlags::GravityEnabled);
		this->MGravityEnabled = InGravityEnabled;
	}

	// Named to match signature of TPBDRigidParticleHandle, as both are used in templated functions.
	// See its comment for details.
	bool& SetDisabledLowLevel() { return MDisabled; }

	const TVector<T, d>& PreV() const { return MPreV; }
	void SetPreV(const TVector<T, d>& InPreV)
	{
		this->MarkDirty(EParticleFlags::PreV);
		this->MPreV = InPreV;
	}

	const TVector<T, d>& PreW() const { return MPreW; }
	void SetPreW(const TVector<T, d>& InPreW)
	{
		this->MarkDirty(EParticleFlags::PreW);
		this->MPreW = InPreW;
	}

	const TVector<T, d>& P() const { return MP; }
	void SetP(const TVector<T, d>& InP)
	{
		this->MarkDirty(EParticleFlags::P);
		this->MP = InP;
	}

	const TRotation<T, d>& Q() const { return MQ; }
	void SetQ(const TRotation<T, d>& InQ)
	{
		this->MarkDirty(EParticleFlags::Q);
		this->MQ = InQ;
	}

	const TVector<T, d>& F() const { return MF; }
	void SetF(const TVector<T, d>& InF)
	{
		this->MarkDirty(EParticleFlags::F);
		this->MF = InF;
	}

	const TVector<T, d>& Torque() const { return MTorque; }
	void SetTorque(const TVector<T, d>& InTorque)
	{
		this->MarkDirty(EParticleFlags::Torque);
		this->MTorque = InTorque;
	}

	const TVector<T, d>& ExternalForce() const { return MExternalForce; }
	void SetExternalForce(const TVector<T, d>& InExternalForce, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::ExternalForce, bInvalidate);
		this->MExternalForce = InExternalForce;
	}

	const TVector<T, d>& ExternalTorque() const { return MExternalTorque; }
	void SetExternalTorque(const TVector<T, d>& InExternalTorque, bool bInvalidate = true)
	{
		this->MarkDirty(EParticleFlags::ExternalTorque, bInvalidate);
		this->MExternalTorque = InExternalTorque;
	}

	const PMatrix<T, d, d>& I() const { return MI; }
	void SetI(const PMatrix<T, d, d>& InI)
	{
		this->MarkDirty(EParticleFlags::I);
		this->MI = InI;
	}

	const PMatrix<T, d, d>& InvI() const { return MInvI; }
	void SetInvI(const PMatrix<T, d, d>& InInvI)
	{
		this->MarkDirty(EParticleFlags::InvI);
		this->MInvI = InInvI;
	}

	T M() const { return MM; }
	void SetM(const T& InM)
	{
		this->MarkDirty(EParticleFlags::M);
		this->MM = InM;
	}

	T InvM() const { return MInvM; }
	void SetInvM(const T& InInvM)
	{
		this->MarkDirty(EParticleFlags::InvM);
		this->MInvM = InInvM;
	}

	int32 Island() const { return MIsland; }
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetIsland(const int32 InIsland)
	{
		this->MIsland = InIsland;
	}

	bool ToBeRemovedOnFracture() const { return MToBeRemovedOnFracture; }
	// TODO(stett): Make the setter private. It is public right now to provide access to proxies.
	void SetToBeRemovedOnFracture(const bool bToBeRemovedOnFracture)
	{
		this->MToBeRemovedOnFracture = bToBeRemovedOnFracture;
	}

	EObjectStateType ObjectState() const { return MObjectState; }
	void SetObjectState(EObjectStateType InState)
	{
		//todo: look at physics thread logic
		MObjectState = InState;
		this->MarkDirty(EParticleFlags::ObjectState);
	}

	FParticleData* NewData() const
	{
		return new TPBDRigidParticleData<T, d>(*this);
	}


private:
	TRotation<T, d> MQ;
	TVector<T, d> MPreV;
	TVector<T, d> MPreW;
	TVector<T, d> MP;
	TVector<T, d> MF;
	TVector<T, d> MTorque;
	TVector<T, d> MExternalForce;
	TVector<T, d> MExternalTorque;
	PMatrix<T, d, d> MI;
	PMatrix<T, d, d> MInvI;
	TUniquePtr<TBVHParticles<T, d>> MCollisionParticles;
	T MM;
	T MInvM;
	int32 MIsland;
	int32 MCollisionGroup;
	EObjectStateType MObjectState;
	bool MDisabled;
	bool MToBeRemovedOnFracture;
	bool MGravityEnabled;
};

template <typename T, int d>
class TPBDGeometryCollectionParticle : public TPBDRigidParticle<T, d>
{
public:
	typedef TPBDRigidParticleData<T, d> FData;
	typedef TPBDGeometryCollectionParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;
public:
	TPBDGeometryCollectionParticle<T,d>(const TPBDRigidParticleParameters<T,d>& DynamicParams = TPBDRigidParticleParameters<T,d>())
		: TPBDRigidParticle<T, d>(DynamicParams)
	{
		Type = EParticleType::GeometryCollection;
	}

	static TUniquePtr<TPBDGeometryCollectionParticle<T, d>> CreateParticle(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
	{
		return TUniquePtr<TPBDGeometryCollectionParticle<T, d>>(new TPBDGeometryCollectionParticle<T, d>(DynamicParams));
	}
};

template <typename T, int d>
class TPBDRigidParticleData : public TKinematicGeometryParticleData<T, d>
{
	typedef TKinematicGeometryParticleData<T, d> Base;
public:

	using TKinematicGeometryParticleData<T, d>::Type;

	TPBDRigidParticleData(EParticleType InType = EParticleType::Rigid)
		: Base(InType)
		, MQ(TRotation<T, d>())
		, MPreV(TVector<T, d>(0))
		, MPreW(TVector<T, d>(0))
		, MP(TVector<T, d>(0))
		, MExternalForce(TVector<T, d>(0))
		, MExternalTorque(TVector<T, d>(0))
		, MI(PMatrix<T, d, d>(0))
		, MInvI(PMatrix<T, d, d>(0))
		, MCollisionParticles(nullptr)
		, MM(T(0))
		, MInvM(T(0))
		, MIsland(INDEX_NONE)
		, MCollisionGroup(0)
		, MObjectState(EObjectStateType::Uninitialized)
		, MDisabled(false)
		, MToBeRemovedOnFracture(false)
		, MGravityEnabled(false)
	{}

	TPBDRigidParticleData(const TPBDRigidParticle<T, d>& InParticle)
		: Base(InParticle)
		, MQ(InParticle.Q())
		, MPreV(InParticle.PreV())
		, MPreW(InParticle.PreW())
		, MP(InParticle.P())
		, MExternalForce(InParticle.ExternalForce())
		, MExternalTorque(InParticle.ExternalTorque())
		, MI(InParticle.I())
		, MInvI(InParticle.InvI())
		, MCollisionParticles(nullptr)
		, MM(InParticle.M())
		, MInvM(InParticle.InvM())
		, MIsland(InParticle.Island())
		, MCollisionGroup(InParticle.CollisionGroup())
		, MObjectState(InParticle.ObjectState())
		, MDisabled(InParticle.Disabled())
		, MToBeRemovedOnFracture(InParticle.ToBeRemovedOnFracture())
		, MGravityEnabled(InParticle.IsGravityEnabled())
	{
		Type = EParticleType::Rigid;
	}


	TRotation<T, d> MQ;
	TVector<T, d> MPreV;
	TVector<T, d> MPreW;
	TVector<T, d> MP;
	TVector<T, d> MExternalForce;
	TVector<T, d> MExternalTorque;
	PMatrix<T, d, d> MI;
	PMatrix<T, d, d> MInvI;
	const TBVHParticles<T, d> * MCollisionParticles;
	T MM;
	T MInvM;
	int32 MIsland;
	int32 MCollisionGroup;
	EObjectStateType MObjectState;
	bool MDisabled;
	bool MToBeRemovedOnFracture;
	bool MGravityEnabled;

	void Reset() {
		TKinematicGeometryParticleData<T, d>::Reset();
		Type = EParticleType::Rigid;
		MQ = TRotation<T, d>();
		MPreV = TVector<T, d>(0);
		MPreW = TVector<T, d>(0);
		MP = TVector<T, d>(0);
		MExternalForce = TVector<T, d>(0);
		MExternalTorque = TVector<T, d>(0);
		MI = PMatrix<T, d, d>(0);
		MInvI = PMatrix<T, d, d>(0);
		MCollisionParticles = nullptr;
		MM = T(0);
		MInvM = T(0);
		MIsland = INDEX_NONE;
		MCollisionGroup = 0;
		MObjectState = EObjectStateType::Uninitialized;
		MDisabled = false;
		MToBeRemovedOnFracture = false;
		MGravityEnabled = false;
	}
};


template <typename T, int d>
const TKinematicGeometryParticle<T, d>* TGeometryParticle<T, d>::CastToKinematicParticle() const
{
	if (Type >= EParticleType::Kinematic)
	{
		return static_cast<const TKinematicGeometryParticle<T, d>*>(this);
	}

	return nullptr;
}

template <typename T, int d>
TKinematicGeometryParticle<T, d>* TGeometryParticle<T, d>::CastToKinematicParticle()
{
	if (Type >= EParticleType::Kinematic)
	{
		return static_cast<TKinematicGeometryParticle<T, d>*>(this);
	}

	return nullptr;
}

template <typename T, int d>
TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::CastToRigidParticle() 
{
	if (Type >= EParticleType::Rigid)
	{
		return static_cast<TPBDRigidParticle<T, d>*>(this);
	}

	return nullptr;
}


template <typename T, int d>
const TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::CastToRigidParticle()  const
{
	if (Type >= EParticleType::Rigid)
	{
		return static_cast<const TPBDRigidParticle<T, d>*>(this);
	}

	return nullptr;
}


template <typename T, int d>
EObjectStateType TGeometryParticle<T, d>::ObjectState() const
{
	const TKinematicGeometryParticle<T, d>* Kin = CastToKinematicParticle();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d>
EObjectStateType TKinematicGeometryParticle<T, d>::ObjectState() const
{
	const TPBDRigidParticle<T, d>* Dyn = CastToRigidParticle();
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}

template <typename T, int d>
TGeometryParticle<T, d>* TGeometryParticle<T, d>::SerializationFactory(FChaosArchive& Ar, TGeometryParticle<T, d>* Serializable)
{
	int8 ObjectType = Ar.IsLoading() ? 0 : (int8)Serializable->Type;
	Ar << ObjectType;
	switch ((EParticleType)ObjectType)
	{
	case EParticleType::Static: if (Ar.IsLoading()) { return new TGeometryParticle<T, d>(); } break;
	case EParticleType::Kinematic: if (Ar.IsLoading()) { return new TKinematicGeometryParticle<T, d>(); } break;
	case EParticleType::Rigid: if (Ar.IsLoading()) { return new TPBDRigidParticle<T, d>(); } break;
	case EParticleType::GeometryCollection: if (Ar.IsLoading()) { return new TPBDGeometryCollectionParticle<T, d>(); } break;
	default:
		check(false);
	}
	return nullptr;
}

template <>
CHAOS_API void Chaos::TGeometryParticle<float, 3>::MarkDirty(const EParticleFlags DirtyBits, bool bInvalidate);

template <typename T, int d>
TAccelerationStructureHandle<T,d>::TAccelerationStructureHandle(TGeometryParticleHandle<T, d>* InHandle)
	: ExternalGeometryParticle(InHandle->GTGeometryParticle())
	, GeometryParticleHandle(InHandle)
	, HashResult(InHandle->GetHashResultLowLevel())
{
}

template <typename T, int d>
TAccelerationStructureHandle<T,d>::TAccelerationStructureHandle(TGeometryParticle<T, d>* InGeometryParticle)
	: ExternalGeometryParticle(InGeometryParticle)
	, GeometryParticleHandle(InGeometryParticle ? InGeometryParticle->Handle() : nullptr)
	, HashResult(InGeometryParticle ? InGeometryParticle->GetHashResultLowLevel() : 0)
{
}

template <typename T, int d>
template <bool bPersistent>
TAccelerationStructureHandle<T, d>::TAccelerationStructureHandle(TGeometryParticleHandleImp<T, d, bPersistent>& InHandle)
	: ExternalGeometryParticle(InHandle.GTGeometryParticle())
	, GeometryParticleHandle(InHandle.Handle())
	, HashResult(InHandle.GetHashResultLowLevel())
{
}

template <typename T, int d>
template <bool bPersistent>
TAccelerationStructureHandle<T, d>::TAccelerationStructureHandle(TGeometryParticleHandleImp<T, d, bPersistent>* InHandle, TGeometryParticle<T, d>* InGeometryParticle)
	: ExternalGeometryParticle(InGeometryParticle)
	, GeometryParticleHandle(InHandle)
	, HashResult(0)
{
	if (GeometryParticleHandle)
	{
		HashResult = GeometryParticleHandle->GetHashResultLowLevel();
	}
	else if (ExternalGeometryParticle)
	{
		HashResult = ExternalGeometryParticle->GetHashResultLowLevel();
	}

	if (GeometryParticleHandle && ExternalGeometryParticle)
	{
		CHAOS_ENSURE(GeometryParticleHandle->GetHashResultLowLevel() == ExternalGeometryParticle->GetHashResultLowLevel());
	}
}

template <typename T, int d>
void TAccelerationStructureHandle<T, d>::Serialize(FChaosArchive& Ar)
{
	Ar << AsAlwaysSerializable(ExternalGeometryParticle);
	Ar << AsAlwaysSerializable(GeometryParticleHandle);

	Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
	if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) < FExternalPhysicsCustomObjectVersion::SerializeHashResult)
	{
		if (GeometryParticleHandle)
		{
			HashResult = GeometryParticleHandle->GetHashResultLowLevel();
		}
		else if (ExternalGeometryParticle)
		{
			HashResult = ExternalGeometryParticle->GetHashResultLowLevel();
		}
		else
		{
			HashResult = 0;
		}

		if (GeometryParticleHandle && ExternalGeometryParticle)
		{
			CHAOS_ENSURE(GeometryParticleHandle->GetHashResultLowLevel() == ExternalGeometryParticle->GetHashResultLowLevel());
		}
	}
	else
	{
		Ar << HashResult;
	}
}

template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TAccelerationStructureHandle<T, d>& AccelerationHandle)
{
	AccelerationHandle.Serialize(Ar);
	return Ar;
}

} // namespace Chaos

