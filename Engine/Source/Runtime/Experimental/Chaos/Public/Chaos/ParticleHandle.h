// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDRigidParticles.h"

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
	Concrete.X() = TVector<T, d>(0);
	Concrete.R() = TRotation<T, d>::Identity;
}

template <typename T, int d, typename FConcrete>
void KinematicGeometryParticleDefaultConstruct(FConcrete& Concrete, const TKinematicGeometryParticleParameters<T, d>& Params)
{
	Concrete.V() = TVector<T, d>(0);
	Concrete.W() = TVector<T, d>(0);
}

template <typename T, int d, typename FConcrete>
void PBDRigidParticleDefaultConstruct(FConcrete& Concrete, const TPBDRigidParticleParameters<T, d>& Params)
{
	//don't bother calling parent since the call gets made by the corresponding hierarchy in FConcrete
	Concrete.CollisionGroup() = 0;
	Concrete.Disabled() = Params.bDisabled;
	Concrete.PreV() = Concrete.V();
	Concrete.PreW() = Concrete.W();
	Concrete.P() = Concrete.X();
	Concrete.Q() = Concrete.R();
	Concrete.F() = TVector<T, d>(0);
	Concrete.Torque() = TVector<T, d>(0);
	Concrete.M() = 1;
	Concrete.InvM() = 1;
	Concrete.I() = PMatrix<T, d, d>(1, 1, 1);
	Concrete.InvI() = PMatrix<T, d, d>(1, 1, 1);
	Concrete.Island() = 0;
	Concrete.ToBeRemovedOnFracture() = false;
	Concrete.SetObjectState(Params.bStartSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic);
}

template <typename FConcrete>
bool GeometryParticleSleeping(const FConcrete& Concrete)
{
	return Concrete.ObjectState() == EObjectStateType::Sleeping;
}

template <typename T, int d>
class TGeometryParticleHandle;

template <typename T, int d>
class TKinematicGeometryParticleHandle;

template <typename T, int d>
class TPBDRigidParticleHandle;

enum class EParticleType : uint8
{
	Static,
	Kinematic,
	Dynamic
};

template <typename T, int d>
class TGeometryParticleHandle
{
public:
	TGeometryParticleHandle(TGeometryParticles<T, d>* InParticles, int32 InParticleIdx, int32 InHandleIdx, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
		: GeometryParticles(InParticles)
		, ParticleIdx(InParticleIdx)
		, HandleIdx(InHandleIdx)
		, Type(EParticleType::Static)
	{
		GeometryParticles->Handle(ParticleIdx) = this;
		GeometryParticleDefaultConstruct<T, d>(*this, Params);
	}

	//NOTE: this is not virtual and only acceptable because we know the children have no extra data. 
	//You must modify the union to extend this class and do not add any member variables
	~TGeometryParticleHandle()	
	{
	}

	TGeometryParticleHandle(const TGeometryParticleHandle&) = delete;

	const TVector<T, d>& X() const { return GeometryParticles->X(ParticleIdx); }
	TVector<T, d>& X() { return GeometryParticles->X(ParticleIdx); }

	const TRotation<T, d>& R() const { return GeometryParticles->R(ParticleIdx); }
	TRotation<T, d>& R() { return GeometryParticles->R(ParticleIdx); }

	TSerializablePtr<TImplicitObject<T, d>> Geometry() const { return GeometryParticles->Geometry(ParticleIdx); }
	
	EObjectStateType ObjectState() const;

	const TKinematicGeometryParticleHandle<T, d>* ToKinematic() const;
	TKinematicGeometryParticleHandle<T, d>& ToKinematic();

	const TPBDRigidParticleHandle<T, d>* ToDynamic() const;
	TPBDRigidParticleHandle<T, d>* ToDynamic();

	bool Sleeping() const { return GeometryParticleSleeping(*this); }

	int32 TransientHandleIdx() const { return HandleIdx; }

	static constexpr EParticleType StaticType() { return EParticleType::Static; }

protected:

	union
	{
		TGeometryParticles<T, d>* GeometryParticles;
		TKinematicGeometryParticles<T, d>* KinematicGeometryParticles;
		TPBDRigidParticles<T, d>* PBDRigidParticles;
	};
	int32 ParticleIdx;	//Index into the particle struct of arrays. Note the index can change
	int32 HandleIdx;	//Index into the handles array. This is useful for binding external attributes. Note the index can change
	EParticleType Type;
};

template <typename T, int d>
class TKinematicGeometryParticleHandle : public TGeometryParticleHandle<T,d>
{
public:
	using TGeometryParticleHandle<T, d>::ParticleIdx;
	using TGeometryParticleHandle<T, d>::KinematicGeometryParticles;
	using TGeometryParticleHandle<T, d>::Type;
	using TGeometryParticleHandle<T, d>::ToDynamic;

	TKinematicGeometryParticleHandle(TKinematicGeometryParticles<T,d>* Particles, int32 InIdx, int32 InGlobalIdx, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T,d>())
		: TGeometryParticleHandle<T, d>(Particles, InIdx, InGlobalIdx, Params)
	{
		Type = EParticleType::Kinematic;
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, Params);
	}

	const TVector<T, d>& V() const { return KinematicGeometryParticles->V(ParticleIdx); }
	TVector<T, d>& V() { return KinematicGeometryParticles->V(ParticleIdx); }

	const TVector<T, d>& W() const { return KinematicGeometryParticles->W(ParticleIdx); }
	TVector<T, d>& W() { return KinematicGeometryParticles->W(ParticleIdx); }

	EObjectStateType ObjectState() const;
	static constexpr EParticleType StaticType() { return EParticleType::Kinematic; }
};

template <typename T, int d>
class TPBDRigidParticleHandle : public TKinematicGeometryParticleHandle<T, d>
{
public:
	using TGeometryParticleHandle<T, d>::ParticleIdx;
	using TGeometryParticleHandle<T, d>::PBDRigidParticles;
	using TGeometryParticleHandle<T, d>::Type;

	TPBDRigidParticleHandle<T, d>(TPBDRigidParticles<T,d>* Particles, int32 InIdx, int32 InGlobalIdx, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticleHandle<T, d>(Particles, InIdx, InGlobalIdx, Params)
	{
		Type = EParticleType::Dynamic;
		PBDRigidParticleDefaultConstruct<T, d>(*this, Params);
	}

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return PBDRigidParticles->CollisionParticles(ParticleIdx); }

	int32 CollisionGroup() const { return PBDRigidParticles->CollisionGroup(ParticleIdx); }
	int32& CollisionGroup() { return PBDRigidParticles->CollisionGroup(ParticleIdx); }

	bool Disabled() const { return PBDRigidParticles->Disabled(ParticleIdx); }
	bool& Disabled() { return PBDRigidParticles->Disabled(ParticleIdx); }

	const TVector<T, d>& PreV() const { return PBDRigidParticles->PreV(ParticleIdx); }
	TVector<T, d>& PreV() { return PBDRigidParticles->PreV(ParticleIdx); }
	const TVector<T, d>& PreW() const { return PBDRigidParticles->PreW(ParticleIdx); }
	TVector<T, d>& PreW() { return PBDRigidParticles->PreW(ParticleIdx); }

	const TVector<T, d>& P() const { return PBDRigidParticles->P(ParticleIdx); }
	TVector<T, d>& P() { return PBDRigidParticles->P(ParticleIdx); }

	const TRotation<T, d>& Q() const { return PBDRigidParticles->Q(ParticleIdx); }
	TRotation<T, d>& Q() { return PBDRigidParticles->Q(ParticleIdx); }

	const TVector<T, d>& F() const { return PBDRigidParticles->F(ParticleIdx); }
	TVector<T, d>& F() { return PBDRigidParticles->F(ParticleIdx); }

	const TVector<T, d>& Torque() const { return PBDRigidParticles->Torque(ParticleIdx); }
	TVector<T, d>& Torque() { return PBDRigidParticles->Torque(ParticleIdx); }

	const PMatrix<T, d, d>& I() const { return PBDRigidParticles->I(ParticleIdx); }
	PMatrix<T, d, d>& I() { return PBDRigidParticles->I(ParticleIdx); }

	const PMatrix<T, d, d>& InvI() const { return PBDRigidParticles->InvI(ParticleIdx); }
	PMatrix<T, d, d>& InvI() { return PBDRigidParticles->InvI(ParticleIdx); }

	T M() const { return PBDRigidParticles->M(ParticleIdx); }
	T& M() { return PBDRigidParticles->M(ParticleIdx); }

	T InvM() const { return PBDRigidParticles->InvM(ParticleIdx); }
	T& InvM() { return PBDRigidParticles->InvM(ParticleIdx); }

	int32 Island() const { return PBDRigidParticles->Island(ParticleIdx); }
	int32& Island() { return PBDRigidParticles->Island(ParticleIdx); }

	bool ToBeRemovedOnFracture() const { return PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx); }
	bool& ToBeRemovedOnFracture() { return PBDRigidParticles->ToBeRemovedOnFracture(ParticleIdx); }

	EObjectStateType ObjectState() const { return PBDRigidParticles->ObjectState(ParticleIdx); }
	void SetObjectState(EObjectStateType InState) { PBDRigidParticles->SetObjectState(ParticleIdx, InState); }
	void SetSleeping(bool bSleeping) { PBDRigidParticles->SetSleeping(ParticleIdx, bSleeping); }

	static constexpr EParticleType StaticType() { return EParticleType::Dynamic; }
};

template <typename T, int d>
const TKinematicGeometryParticleHandle<T, d>* TGeometryParticleHandle<T,d>::ToKinematic() const { return Type >= EParticleType::Kinematic ? static_cast<const TKinematicGeometryParticleHandle<T, d>*>(this) : nullptr; }
template <typename T, int d>
TKinematicGeometryParticleHandle<T, d>& TGeometryParticleHandle<T, d>::ToKinematic() { return Type >= EParticleType::Kinematic ? static_cast<TKinematicGeometryParticleHandle<T, d>*>(this) : nullptr; }

template <typename T, int d>
const TPBDRigidParticleHandle<T, d>* TGeometryParticleHandle<T, d>::ToDynamic() const { return Type >= EParticleType::Dynamic ? static_cast<const TPBDRigidParticleHandle<T, d>*>(this) : nullptr; }
template <typename T, int d>
TPBDRigidParticleHandle<T, d>* TGeometryParticleHandle<T, d>::ToDynamic() { return Type >= EParticleType::Dynamic ? static_cast<TPBDRigidParticleHandle<T, d>*>(this) : nullptr; }

template <typename T, int d>
EObjectStateType TGeometryParticleHandle<T,d>::ObjectState() const
{
	const TKinematicGeometryParticleHandle<T, d>* Kin = ToKinematic();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d>
EObjectStateType TKinematicGeometryParticleHandle<T, d>::ObjectState() const
{
	const TPBDRigidParticleHandle<T, d>* Dyn = ToDynamic();
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}

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

	const TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) const { return Handles[Idx];}
	TUniquePtr<TGeometryParticleHandle<T, d>>& Handle(int32 Idx) { return Handles[Idx]; }
private:
	TArrayCollectionArray<TUniquePtr<TGeometryParticleHandle<T, d>>> Handles;
};

template <typename T, int d>
class TKinematicGeometryParticle;

template <typename T, int d>
class TPBDRigidParticle;

template <typename T, int d>
class TGeometryParticle
{
public:
	TGeometryParticle(const TGeometryParticleParameters<T, d>& StaticParams = TGeometryParticleParameters<T, d>())
	{
		Type = EParticleType::Static;
		GeometryParticleDefaultConstruct<T, d>(*this, StaticParams);
	}

	virtual ~TGeometryParticle() {}	//only virtual for easier memory management. Should generally be a static API

	TGeometryParticle(const TGeometryParticle&) = delete;

	const TVector<T, d>& X() const { return this->MX; }
	TVector<T, d>& X() { return this->MX; }

	const TRotation<T, d>& R() const { return this->MR; }
	TRotation<T, d>& R() { return this->MR; }

	TSerializablePtr<TImplicitObject<T, d>> Geometry() const { return this->MGeometry; }

	EObjectStateType ObjectState() const;

	const TKinematicGeometryParticle<T, d>* ToKinematic() const;
	TKinematicGeometryParticle<T, d>& ToKinematic();

	const TPBDRigidParticle<T, d>* ToDynamic() const;
	TPBDRigidParticle<T, d>* ToDynamic();

private:
	TVector<T, d> MX;
	TRotation<T, d> MR;
protected:
	EParticleType Type;
};

template <typename T, int d>
class TKinematicGeometryParticle : public TGeometryParticle<T, d>
{
public:
	using TGeometryParticle<T, d>::Type;
	using TGeometryParticle<T, d>::ToDynamic;

	TKinematicGeometryParticle(const TKinematicGeometryParticleParameters<T, d>& KinematicParams = TKinematicGeometryParticleParameters<T,d>())
		: TGeometryParticle<T, d>(KinematicParams)
	{
		Type = EParticleType::Kinematic;
		KinematicGeometryParticleDefaultConstruct<T, d>(*this, KinematicParams);
	}

	const TVector<T, d>& V() const { return MV; }
	TVector<T, d>& V() { return MV; }

	const TVector<T, d>& W() const { return MW; }
	TVector<T, d>& W() { return MW; }

	EObjectStateType ObjectState() const;

private:
	TVector<T, d> MV;
	TVector<T, d> MW;
};

template <typename T, int d>
class TPBDRigidParticle : public TKinematicGeometryParticle<T, d>
{
public:
	using TGeometryParticle<T, d>::Type;

	TPBDRigidParticle<T, d>(const TPBDRigidParticleParameters<T, d>& DynamicParams = TPBDRigidParticleParameters<T, d>())
		: TKinematicGeometryParticle<T, d>(DynamicParams)
	{
		Type = EParticleType::Dynamic;
		PBDRigidParticleDefaultConstruct<T, d>(*this, DynamicParams);
	}

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return MCollisionParticles; }

	int32 CollisionGroup() const { return MCollisionGroup; }
	int32& CollisionGroup() { return MCollisionGroup; }

	bool Disabled() const { return MDisabled; }
	bool& Disabled() { return MDisabled; }

	const TVector<T, d>& PreV() const { return MPreV; }
	TVector<T, d>& PreV() { return MPreV; }
	const TVector<T, d>& PreW() const { return MPreW; }
	TVector<T, d>& PreW() { return MPreW; }

	const TVector<T, d>& P() const { return MP; }
	TVector<T, d>& P() { return MP; }

	const TVector<T, d>& Q() const { return MQ; }
	TRotation<T, d>& Q() { return MQ; }

	const TVector<T, d>& F() const { return MF; }
	TVector<T, d>& F() { return MF; }

	const TVector<T, d>& Torque() const { return MTorque; }
	TVector<T, d>& Torque() { return MTorque; }

	const PMatrix<T, d, d>& I() const { return MI; }
	PMatrix<T, d, d>& I() { return MI; }

	const PMatrix<T, d, d>& InvI() const { return MInvI; }
	PMatrix<T, d, d>& InvI() { return MInvI; }

	T M() const { return MM; }
	T& M() { return MM; }

	T InvM() const { return MInvM; }
	T& InvM() { return MInvM; }

	int32 Island() const { return MIsland; }
	int32& Island() { return MIsland; }

	bool ToBeRemovedOnFracture() const { return MToBeRemovedOnFracture; }
	bool& ToBeRemovedOnFracture() { return MToBeRemovedOnFracture; }

	EObjectStateType ObjectState() const { return MObjectState; }
	void SetObjectState(EObjectStateType InState) { MObjectState = InState; }  //todo: look at physics thread logic 

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
const TKinematicGeometryParticle<T, d>* TGeometryParticle<T, d>::ToKinematic() const { return Type >= EParticleType::Kinematic ? static_cast<const TKinematicGeometryParticle<T, d>*>(this) : nullptr; }
template <typename T, int d>
TKinematicGeometryParticle<T, d>& TGeometryParticle<T, d>::ToKinematic() { return Type >= EParticleType::Kinematic ? static_cast<TKinematicGeometryParticle<T, d>*>(this) : nullptr; }

template <typename T, int d>
const TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::ToDynamic() const { return Type >= EParticleType::Dynamic ? static_cast<const TPBDRigidParticle<T, d>*>(this) : nullptr; }
template <typename T, int d>
TPBDRigidParticle<T, d>* TGeometryParticle<T, d>::ToDynamic() { return Type >= EParticleType::Dynamic ? static_cast<TPBDRigidParticle<T, d>*>(this) : nullptr; }

template <typename T, int d>
EObjectStateType TGeometryParticle<T, d>::ObjectState() const
{
	const TKinematicGeometryParticle<T, d>* Kin = ToKinematic();
	return Kin ? Kin->ObjectState() : EObjectStateType::Static;
}

template <typename T, int d>
EObjectStateType TKinematicGeometryParticle<T, d>::ObjectState() const
{
	const TPBDRigidParticle<T, d>* Dyn = ToDynamic();
	return Dyn ? Dyn->ObjectState() : EObjectStateType::Kinematic;
}

template <typename T, int d, typename TSOA>
class TConstParticleIterator
{
public:
	TConstParticleIterator()
		: ParticleIdx(0)
		, SOAIdx(0)
		, CurSOA(nullptr)
	{
	}
	
	TConstParticleIterator(TArray<TSOA*>&& InSOAs)
		: SOAs(MoveTemp(InSOAs))
		, ParticleIdx(0)
		, SOAIdx(0)
		, CurSOA(SOAs.Num() ? SOAs[0] : nullptr)
	{
	}
	
	operator bool() const { return CurSOA && ParticleIdx < CurSOA->Size(); }
	TConstParticleIterator<T, d, TSOA>& operator++()
	{
		++ParticleIdx;
		if (ParticleIdx >= CurSOA->Size())
		{
			++SOAIdx;
			CurSOA = SOAIdx < SOAs.Num() ? SOAs[SOAIdx] : nullptr;
			ParticleIdx = 0;
		}
		return *this;
	}

	const typename TSOA::THandleType* Handle() const { return CurSOA->Handle(ParticleIdx); }

	//Full PBDRigid API, we rely on templates only instantiating the functions they use to avoid compiler errors on more base particle types
	const TVector<T, d>& X() const { return CurSOA->X(ParticleIdx); }
	const TRotation<T, d>& R() const { return CurSOA->R(ParticleIdx); }
	TSerializablePtr<TImplicitObject<T, d>> Geometry() const { return CurSOA->Geometry(ParticleIdx); }

	const TVector<T, d>& V() const { return CurSOA->V(ParticleIdx); }
	const TVector<T, d>& W() const { return CurSOA->W(ParticleIdx); }

	const TUniquePtr<TBVHParticles<T, d>>& CollisionParticles() const { return CurSOA->CollisionParticles(ParticleIdx); }
	int32 CollisionGroup() const { return CurSOA->CollisionGroup(ParticleIdx); }
	bool Disabled() const { return CurSOA->Disabled(ParticleIdx); }
	const TVector<T, d>& PreW() const { return CurSOA->PreW(ParticleIdx); }
	const TVector<T, d>& P() const { return CurSOA->P(ParticleIdx); }
	const TRotation<T, d>& Q() const { return CurSOA->Q(ParticleIdx); }
	const TVector<T, d>& F() const { return CurSOA->F(ParticleIdx); }
	const TVector<T, d>& Torque() const { return CurSOA->Torque(ParticleIdx); }
	const PMatrix<T, d, d>& I() const { return CurSOA->I(ParticleIdx); }
	const PMatrix<T, d, d>& InvI() const { return CurSOA->InvI(ParticleIdx); }
	T M() const { return CurSOA->M(ParticleIdx); }
	T InvM() const { return CurSOA->InvM(ParticleIdx); }
	int32 Island() const { return CurSOA->Island(ParticleIdx); }
	bool ToBeRemovedOnFracture() const { return CurSOA->ToBeRemovedOnFracture(ParticleIdx); }

	//Note: this won't compile for anything but dynamic even though static handles support it. This should not be necessary even at the handle level
	EObjectStateType ObjectState() const { return CurSOA->ObjectState(ParticleIdx); }

protected:
	TArray<TSOA*> SOAs;
	uint32 ParticleIdx;
	int32 SOAIdx;
	union
	{
		const TSOA* CurSOA;
		TSOA* NonConstCurSOA;	//only here for the non const iterator
	};
};

template <typename T, int d, typename TSOA>
class TParticleIterator : public TConstParticleIterator<T,d,TSOA>
{
public:
	using Base = TConstParticleIterator<T, d, TSOA>;
	using Base::NonConstCurSOA;
	using Base::ParticleIdx;

	TParticleIterator(TSOA* InSOA = nullptr)
		: Base(InSOA)
	{
	}
	TParticleIterator(TArray<TSOA*>&& InSOAs)
		: Base(MoveTemp(InSOAs))
	{
	}


	typename TSOA::THandleType* Handle() { return NonConstCurSOA->Handle(ParticleIdx); }

	//Full PBDRigid API, we rely on templates only instantiating the functions they use to avoid compiler errors on more base particle types
	TVector<T, d>& X() { return NonConstCurSOA->X(ParticleIdx); }
	TRotation<T, d>& R() { return NonConstCurSOA->R(ParticleIdx); }

	TVector<T, d>& V() { return NonConstCurSOA->V(ParticleIdx); }
	TVector<T, d>& W() { return NonConstCurSOA->W(ParticleIdx); }

	int32& CollisionGroup() { return NonConstCurSOA->CollisionGroup(ParticleIdx); }
	bool& Disabled() { return NonConstCurSOA->Disabled(ParticleIdx); }
	TVector<T, d>& PreV() { return NonConstCurSOA->PreV(ParticleIdx); }
	TVector<T, d>& PreW() { return NonConstCurSOA->PreW(ParticleIdx); }
	TVector<T, d>& P() { return NonConstCurSOA->P(ParticleIdx); }
	TRotation<T, d>& Q() { return NonConstCurSOA->Q(ParticleIdx); }
	TVector<T, d>& F() { return NonConstCurSOA->F(ParticleIdx); }
	TVector<T, d>& Torque() { return NonConstCurSOA->Torque(ParticleIdx); }
	PMatrix<T, d, d>& I() { return NonConstCurSOA->I(ParticleIdx); }
	PMatrix<T, d, d>& InvI() { return NonConstCurSOA->InvI(ParticleIdx); }
	T& M() { return NonConstCurSOA->M(ParticleIdx); }
	T& InvM() { return NonConstCurSOA->InvM(ParticleIdx); }
	int32& Island() { return NonConstCurSOA->Island(ParticleIdx); }
	bool& ToBeRemovedOnFracture() { return NonConstCurSOA->ToBeRemovedOnFracture(ParticleIdx); }
	void SetObjectState(EObjectStateType InState) { NonConstCurSOA->SetObjectState(ParticleIdx, InState); }
	void SetSleeping(bool bSleeping) { NonConstCurSOA->SetSleeping(ParticleIdx, bSleeping); }
};

template <typename T, int d, typename TSOA>
TParticleIterator<T, d, TSOA> MakeParticleIterator(TArray<TSOA*>&& SOAs)
{
	return TParticleIterator<T, d, TSOA>(MoveTemp(SOAs));
}

template <typename T, int d, typename TSOA>
TConstParticleIterator<T, d, TSOA> MakeConstParticleIterator(TArray<TSOA*>&& SOAs)
{
	return TConstParticleIterator<T, d, TSOA>(MoveTemp(SOAs));
}


}