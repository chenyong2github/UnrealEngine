// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidClusteredParticles.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ParticleIterator.h"
#include "Chaos/ParticleDirtyFlags.h"

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
	{}
	bool bStartSleeping;
};

/** Concrete can either be the game thread or physics representation, but API stays the same. Useful for keeping initialization and other logic the same*/
template <typename T, int d, typename FConcrete>
void GeometryParticleDefaultConstruct(FConcrete& Concrete, const TGeometryParticleParameters<T,d>& Params)
{
	Concrete.SetX(TVector<T, d>(0));
	Concrete.SetR(TRotation<T, d>::Identity);
}

template <typename T, int d, typename FConcrete>
void KinematicGeometryParticleDefaultConstruct(FConcrete& Concrete, const TKinematicGeometryParticleParameters<T, d>& Params)
{
	Concrete.SetV(TVector<T, d>(0));
	Concrete.SetW(TVector<T, d>(0));
}

template <typename T, int d, typename FConcrete>
void PBDRigidParticleDefaultConstruct(FConcrete& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
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
	Concrete.SetM(1);
	Concrete.SetInvM(1);
	Concrete.SetI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetInvI(PMatrix<T, d, d>(1, 1, 1));
	Concrete.SetObjectState(Params.bStartSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic);
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

template <typename T, int d>
class TParticleHandleBase
{
public:
	using TType = T;
	static constexpr int D = d;

	TParticleHandleBase(TGeometryParticles<T, d>* InParticles = nullptr, int32 InParticleIdx = 0)
	: GeometryParticles(InParticles)
	, ParticleIdx(InParticleIdx)
	, Type(InParticles ? InParticles->ParticleType() : EParticleType::Static)
	{
	}

	//NOTE: this is not virtual and only acceptable because we know the children have no extra data that requires destruction. 
	//You must modify the union to extend this class and do not add any member variables
	~TParticleHandleBase()
	{
	}


protected:
	union
	{
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

	TGeometryParticleHandleImp(TGeometryParticles<T, d>* InParticles, int32 InParticleIdx, int32 InHandleIdx, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
		: TParticleHandleBase<T,d>(InParticles, InParticleIdx)
		, HandleIdx(InHandleIdx)
	{
		GeometryParticles->Handle(ParticleIdx) = this;
		GeometryParticleDefaultConstruct<T, d>(*this, Params);
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

	TSerializablePtr<TImplicitObject<T, d>> Geometry() const { return GeometryParticles->Geometry(ParticleIdx); }
	void SetGeometry(TSerializablePtr<TImplicitObject<T, d>> InGeometry) { GeometryParticles->SetGeometry(ParticleIdx, InGeometry); }

	TSerializablePtr<TImplicitObject<T, d>> SharedGeometry() const { return GeometryParticles->SharedGeometry(ParticleIdx); }
	void SetSharedGeometry(TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe> InGeometry) { GeometryParticles->SetSharedGeometry(ParticleIdx, InGeometry); }

	const TUniquePtr<TImplicitObject<T, d>>& DynamicGeometry() const { return GeometryParticles->DynamicGeometry(ParticleIdx); }
	void SetDynamicGeometry(TUniquePtr<TImplicitObject<T, d>>&& Unique) { GeometryParticles->SetDynamicGeometry(ParticleIdx, MoveTemp(Unique)); }

	const TShapesArray<T,d>& ShapesArray() const { return GeometryParticles->ShapesArray(ParticleIdx); }
	
	EObjectStateType ObjectState() const;

	TGeometryParticle<T, d>* GTGeometryParticle() const { return GeometryParticles->GTGeometryParticle(ParticleIdx); }
	TGeometryParticle<T, d>*& GTGeometryParticle() { return GeometryParticles->GTGeometryParticle(ParticleIdx); }

	const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* AsKinematic() const;
	TKinematicGeometryParticleHandleImp<T, d, bPersistent>* AsKinematic();

	const TPBDRigidParticleHandleImp<T, d, bPersistent>* AsDynamic() const;
	TPBDRigidParticleHandleImp<T, d, bPersistent>* AsDynamic();

	const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* AsClustered() const;
	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* AsClustered();

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

protected:

	friend TGeometryParticleHandles<T, d>;
	
	struct FInvalidFromTransient {};
	typename std::conditional<bPersistent, int32, FInvalidFromTransient>::type HandleIdx;	//Index into the handles array. This is useful for binding external attributes. Note the index can change
};

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
	using TGeometryParticleHandleImp<T, d, bPersistent>::AsDynamic;
	using TTransientHandle = TTransientKinematicGeometryParticleHandle<T, d>;
	using TSOAType = TKinematicGeometryParticles<T, d>;

	TKinematicGeometryParticleHandleImp(TKinematicGeometryParticles<T,d>* Particles, int32 InIdx, int32 InGlobalIdx, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T,d>())
		: TGeometryParticleHandleImp<T, d, bPersistent>(Particles, InIdx, InGlobalIdx, Params)
	{
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, Params);
	}

	const TVector<T, d>& V() const { return KinematicGeometryParticles->V(ParticleIdx); }
	TVector<T, d>& V() { return KinematicGeometryParticles->V(ParticleIdx); }
	void SetV(const TVector<T, d>& InV) { KinematicGeometryParticles->V(ParticleIdx) = InV; }

	const TVector<T, d>& W() const { return KinematicGeometryParticles->W(ParticleIdx); }
	TVector<T, d>& W() { return KinematicGeometryParticles->W(ParticleIdx); }
	void SetW(const TVector<T, d>& InW) { KinematicGeometryParticles->W(ParticleIdx) = InW; }

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


	TPBDRigidParticleHandleImp<T, d, bPersistent>(TPBDRigidParticles<T,d>* Particles, int32 InIdx, int32 InGlobalIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticleHandleImp<T, d, bPersistent>(Particles, InIdx, InGlobalIdx, Params)
	{
		PBDRigidParticleDefaultConstruct<T, d>(*this, Params);
		SetIsland(INDEX_NONE);
		SetToBeRemovedOnFracture(false);
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
	void SetObjectState(EObjectStateType InState) { PBDRigidParticles->SetObjectState(ParticleIdx, InState); }
	void SetSleeping(bool bSleeping) { PBDRigidParticles->SetSleeping(ParticleIdx, bSleeping); }

	//Really only useful when using a transient handle
	const TPBDRigidParticleHandleImp<T, d, true>* Handle() const { return PBDRigidParticles->Handle(ParticleIdx); }
	TPBDRigidParticleHandleImp<T, d, true>* Handle() { return PBDRigidParticles->Handle(ParticleIdx); }

	static constexpr EParticleType StaticType() { return EParticleType::Dynamic; }
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


	TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>(TPBDRigidClusteredParticles<T, d>* Particles, int32 InIdx, int32 InGlobalIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
		: TPBDRigidParticleHandleImp<T, d, bPersistent>(Particles, InIdx, InGlobalIdx, Params)
	{
		PBDRigidClusteredParticleDefaultConstruct<T, d>(*this, Params);
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

	static constexpr EParticleType StaticType() { return EParticleType::Dynamic; }

	int32 TransientParticleIndex() const { return ParticleIdx; }
};

template <typename T, int d, bool bPersistent>
const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T,d, bPersistent>::AsKinematic() const { return Type >= EParticleType::Kinematic ? static_cast<const TKinematicGeometryParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }
template <typename T, int d, bool bPersistent>
TKinematicGeometryParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::AsKinematic() { return Type >= EParticleType::Kinematic ? static_cast<TKinematicGeometryParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TPBDRigidParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::AsDynamic() const { return Type >= EParticleType::Dynamic ? static_cast<const TPBDRigidParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }
template <typename T, int d, bool bPersistent>
TPBDRigidParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::AsDynamic() { return Type >= EParticleType::Dynamic ? static_cast<TPBDRigidParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::AsClustered() const { return Type >= EParticleType::Clustered ? static_cast<const TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }
template <typename T, int d, bool bPersistent>
TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>* TGeometryParticleHandleImp<T, d, bPersistent>::AsClustered() { return Type >= EParticleType::Clustered ? static_cast<TPBDRigidClusteredParticleHandleImp<T, d, bPersistent>*>(this) : nullptr; }

template <typename T, int d, bool bPersistent>
EObjectStateType TGeometryParticleHandleImp<T,d, bPersistent>::ObjectState() const
{
	const TKinematicGeometryParticleHandleImp<T, d, bPersistent>* Kin = AsKinematic();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d, bool bPersistent>
EObjectStateType TKinematicGeometryParticleHandleImp<T, d, bPersistent>::ObjectState() const
{
	const TPBDRigidParticleHandleImp<T, d, bPersistent>* Dyn = AsDynamic();
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
	case EParticleType::Dynamic:
		return FString::Printf(TEXT("Dynamic[%d]"), ParticleIdx);
		break;
	}
	return FString();
}

/**
 * A wrapper around any type of particle handle to provide a consistent (read-only) API for all particle types.
 * This can make code simpler because you can write code that is type-agnostic, but it
 * has a cost. Where possible it is better to write code that is specific to the type(s)
 * of particles being operated on. TGenericParticleHandle has pointer symatics, so you can use one wherever
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

		// Static Particles
		const TVector<T, d>& X() const { return Handle->X(); }
		const TRotation<T, d>& R() const { return Handle->R(); }
		TSerializablePtr<TImplicitObject<T, d>> Geometry() const { return Handle->Geometry(); }
		const TUniquePtr<TImplicitObject<T, d>>& DynamicGeometry() const { return Handle->DynamicGeometry(); }

		// Kinematic Particles
		const TVector<T, d>& V() const { return (Handle->AsKinematic()) ? Handle->AsKinematic()->V() : ZeroVector; }
		const TVector<T, d>& W() const { return (Handle->AsKinematic()) ? Handle->AsKinematic()->W() : ZeroVector; }

		// Dynamic Particles
		int32 CollisionParticlesSize() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->CollisionParticlesSize() : 0; }
		const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->CollisionParticles() : NullBVHParticles; }
		int32 CollisionGroup() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->CollisionGroup() : 0; }
		bool Disabled() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->Disabled() : false; }				// @todo(ccaulfield): should be available on all types?
		const TVector<T, d>& PreV() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->PreV() : ZeroVector; }	// @todo(ccaulfield): should be available on kinematics?
		const TVector<T, d>& PreW() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->PreW() : ZeroVector; }	// @todo(ccaulfield): should be available on kinematics?
		const TVector<T, d>& P() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->P() : X(); }
		const TRotation<T, d>& Q() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->Q() : R(); }
		const TVector<T, d>& F() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->F() : ZeroVector; }
		const TVector<T, d>& Torque() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->Torque() : ZeroVector; }
		const PMatrix<T, d, d>& I() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->I() : ZeroMatrix; }
		const PMatrix<T, d, d>& InvI() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->InvI() : ZeroMatrix; }
		T M() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->M() : (T)0; }
		T InvM() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->InvM() : (T)0; }
		int32 Island() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->Island() : INDEX_NONE; }
		bool ToBeRemovedOnFracture() const { return (Handle->AsDynamic()) ? Handle->AsDynamic()->ToBeRemovedOnFracture() : false; }

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

	const TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) const { return Handles[Idx];}
	TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) { return Handles[Idx]; }
private:
	TArrayCollectionArray<TUniquePtr<TGeometryParticleHandleImp<T, d, true>>> Handles;
};

template <typename T, int d>
class TKinematicGeometryParticle;

template <typename T, int d>
class TPBDRigidParticle;


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

	TGeometryParticle(const TGeometryParticleParameters<T, d>& StaticParams = TGeometryParticleParameters<T, d>())
		: MUserData(nullptr)
	{
		Type = EParticleType::Static;
		Proxy = nullptr;
		GeometryParticleDefaultConstruct<T, d>(*this, StaticParams);
	}

	virtual ~TGeometryParticle() {}	//only virtual for easier memory management. Should generally be a static API

	TGeometryParticle(const TGeometryParticle&) = delete;

	TGeometryParticle& operator=(const TGeometryParticle&) = delete;

	const TVector<T, d>& X() const { return this->MX; }
	void SetX(const TVector<T, d>& InX)
	{
		this->MarkDirty(EParticleFlags::X);
		this->MX = InX;
	}

	const TRotation<T, d>& R() const { return this->MR; }
	void SetR(const TRotation<T, d>& InR)
	{
		this->MarkDirty(EParticleFlags::R);
		this->MR = InR;
	}

	//todo: geometry should not be owned by particle
	void SetGeometry(TUniquePtr<TImplicitObject<T, d>>&& UniqueGeometry)
	{
		// Take ownership of the geometry, putting it into a shared ptr.
		// This is necessary because we cannot be sure whether the particle
		// will be destroyed on the game thread or physics thread first,
		// but geometry data is shared between them.
		TImplicitObject<T, d>* RawGeometry = UniqueGeometry.Release();
		SetGeometry(TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe>(RawGeometry));
	}

	// TODO: Right now this method exists so we can do things like FPhysTestSerializer::CreateChaosData.
	//       We should replace this with a method for supporting SetGeometry(RawGeometry).
	void SetGeometry(TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe> SharedGeometry)
	{
		this->MarkDirty(EParticleFlags::Geometry);
		this->MGeometry = SharedGeometry;
		UpdateShapesArray();
	}

	void SetGeometry(TSerializablePtr<TImplicitObject<T, d>> RawGeometry)
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

	//Note: this must be called after setting geometry. This API seems bad. Should probably be part of setting geometry
	void SetShapesArray(TShapesArray<T, d>&& InShapesArray)
	{
		ensure(InShapesArray.Num() == MShapesArray.Num());
		MShapesArray = MoveTemp(InShapesArray);
	}

	TSerializablePtr<TImplicitObject<T, d>> Geometry() const { return MakeSerializable(MGeometry); }

	const TShapesArray<T,d>& ShapesArray() const { return MShapesArray; }

	EObjectStateType ObjectState() const;

	EParticleType ObjectType() const
	{
		return Type;
	}

	const TKinematicGeometryParticle<T, d>* AsKinematic() const;
	TKinematicGeometryParticle<T, d>* AsKinematic();

	const TPBDRigidParticle<T, d>* AsDynamic() const;
	TPBDRigidParticle<T, d>* AsDynamic();

	FParticleData* NewData() const { return new TGeometryParticleData<T, d>( *this ); }

	bool IsDirty() const
	{
		return this->MDirtyFlags.IsDirty();
	}

	bool IsDirty(const EParticleFlags CheckBits) const
	{
		return this->MDirtyFlags.IsDirty(CheckBits);
	}

	// Pointer to any data that the solver wants to associate with this particle
	// TODO: It's important to eventually hide this!
	// Right now it's exposed to lubricate the creation of the whole proxy system.
	class IPhysicsProxyBase* Proxy;

private:
	TVector<T, d> MX;
	TRotation<T, d> MR;
	TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe> MGeometry;	//TODO: geometry should live in bodysetup
	TShapesArray<T,d> MShapesArray;

	// Pointer to some arbitrary data associated with the particle, but not used by Chaos. External systems may use this for whatever.
	void* MUserData;

	// This is only for use by ParticleData. This should be called only in one place,
	// when the geometry is being copied from GT to PT.
	TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe> GeometrySharedLowLevel() const
	{
		return MGeometry;
	}

protected:
	EParticleType Type;
	FParticleDirtyFlags MDirtyFlags;

	void MarkDirty(const EParticleFlags DirtyBits)
	{
		this->MDirtyFlags.MarkDirty(DirtyBits);
	}

	void UpdateShapesArray()
	{
		UpdateShapesArrayFromGeometry<T, d>(MShapesArray, MGeometry.Get());
	}
};

template <typename T, int d>
class TGeometryParticleData : public FParticleData
{
	typedef FParticleData Base;
public:

	TGeometryParticleData(EParticleType InType = EParticleType::Static)
		: FParticleData(InType)
		, X(TVector<T, d>(0))
		, R(TRotation<T, d>())
	{}

	TGeometryParticleData(const TGeometryParticle<T,d>& InParticle)
		: FParticleData(EParticleType::Static)
		, X(InParticle.X())
		, R(InParticle.R())
		, Geometry(InParticle.GeometrySharedLowLevel())
	{}

	void Reset() 
	{ 
		FParticleData::Reset();  
		X = TVector<T, d>(0); 
		R = TRotation<T, d>(); 
		Geometry = TSharedPtr<TImplicitObjectUnion<T, d>, ESPMode::ThreadSafe>();
	}

	TVector<T, d> X;
	TRotation<T, d> R;
	TSharedPtr<TImplicitObject<T, d>, ESPMode::ThreadSafe> Geometry;
};


template <typename T, int d>
class TKinematicGeometryParticle : public TGeometryParticle<T, d>
{
public:
	typedef TKinematicGeometryParticleData<T, d> FData;
	typedef TKinematicGeometryParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;
	using TGeometryParticle<T, d>::AsDynamic;

	TKinematicGeometryParticle(const TKinematicGeometryParticleParameters<T, d>& KinematicParams = TKinematicGeometryParticleParameters<T,d>())
		: TGeometryParticle<T, d>(KinematicParams)
	{
		Type = EParticleType::Kinematic;
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, KinematicParams);
	}

	const TVector<T, d>& V() const { return MV; }
	void SetV(const TVector<T, d>& InV)
	{
		this->MarkDirty(EParticleFlags::V);
		this->MV = InV;
	}

	const TVector<T, d>& W() const { return MW; }
	void SetW(const TVector<T, d>& InW)
	{
		this->MarkDirty(EParticleFlags::W);
		this->MW = InW;
	}

	EObjectStateType ObjectState() const;

	FParticleData* NewData() const
	{
		return new TKinematicGeometryParticleData<T, d>(*this);
	}

private:
	TVector<T, d> MV;
	TVector<T, d> MW;
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
		, MW(TVector<T, d>(0)) {}

	TKinematicGeometryParticleData(const TKinematicGeometryParticle<T, d>& InParticle)
		: Base(InParticle)
		, MV(InParticle.V())
		, MW(InParticle.W()) 
	{
		Type = EParticleType::Kinematic;
	}


	void Reset() {
		TGeometryParticleData<T, d>::Reset();
		Type = EParticleType::Kinematic;
		MV = TVector<T, d>(0); MW = TVector<T, d>();
	}

	TVector<T, d> MV;
	TVector<T, d> MW;
};


template <typename T, int d>
class TPBDRigidParticle : public TKinematicGeometryParticle<T, d>
{
public:
	typedef TPBDRigidParticleData<T, d> FData;
	typedef TPBDRigidParticleHandle<T, d> FHandle;

	using TGeometryParticle<T, d>::Type;

	TPBDRigidParticle<T, d>(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticle<T, d>(DynamicParams)
	{
		Type = EParticleType::Dynamic;
		MIsland = INDEX_NONE;
		MToBeRemovedOnFracture = false;
		PBDRigidParticleDefaultConstruct<T, d>(*this, DynamicParams);
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

};

template <typename T, int d>
class TPBDRigidParticleData : public TKinematicGeometryParticleData<T, d>
{
	typedef TKinematicGeometryParticleData<T, d> Base;
public:

	using TKinematicGeometryParticleData<T, d>::Type;

	TPBDRigidParticleData(EParticleType InType = EParticleType::Dynamic)
		: Base(InType)
		, MQ(TRotation<T, d>())
		, MPreV(TVector<T, d>(0))
		, MPreW(TVector<T, d>(0))
		, MP(TVector<T, d>(0))
		, MF(TVector<T, d>(0))
		, MTorque(TVector<T, d>(0))
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
	{}

	TPBDRigidParticleData(const TPBDRigidParticle<T, d>& InParticle)
		: Base(InParticle)
		, MQ(InParticle.Q())
		, MPreV(InParticle.PreV())
		, MPreW(InParticle.PreW())
		, MP(InParticle.P())
		, MF(InParticle.F())
		, MTorque(InParticle.Torque())
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
	{
		Type = EParticleType::Dynamic;
	}


	TRotation<T, d> MQ;
	TVector<T, d> MPreV;
	TVector<T, d> MPreW;
	TVector<T, d> MP;
	TVector<T, d> MF;
	TVector<T, d> MTorque;
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

	void Reset() {
		TKinematicGeometryParticleData<T, d>::Reset();
		Type = EParticleType::Dynamic;
		MQ = TRotation<T, d>();
		MPreV = TVector<T, d>(0);
		MPreW = TVector<T, d>(0);
		MP = TVector<T, d>(0);
		MF = TVector<T, d>(0);
		MTorque = TVector<T, d>(0);
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
	}
};


template <typename T, int d>
const TKinematicGeometryParticle<T, d>* TGeometryParticle<T, d>::AsKinematic() const { return Type >= EParticleType::Kinematic ? static_cast<const TKinematicGeometryParticle<T, d>*>(this) : nullptr; }
template <typename T, int d>
TKinematicGeometryParticle<T, d>* TGeometryParticle<T, d>::AsKinematic() { return Type >= EParticleType::Kinematic ? static_cast<TKinematicGeometryParticle<T, d>*>(this) : nullptr; }

template <typename T, int d>
const TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::AsDynamic() const { return Type >= EParticleType::Dynamic ? static_cast<const TPBDRigidParticle<T, d>*>(this) : nullptr; }
template <typename T, int d>
TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::AsDynamic() { return Type >= EParticleType::Dynamic ? static_cast<TPBDRigidParticle<T, d>*>(this) : nullptr; }

template <typename T, int d>
EObjectStateType TGeometryParticle<T, d>::ObjectState() const
{
	const TKinematicGeometryParticle<T, d>* Kin = AsKinematic();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d>
EObjectStateType TKinematicGeometryParticle<T, d>::ObjectState() const
{
	const TPBDRigidParticle<T, d>* Dyn = AsDynamic();
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}

}
