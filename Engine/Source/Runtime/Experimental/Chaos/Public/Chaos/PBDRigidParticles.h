// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/RigidParticles.h"
#include "Chaos/Rotation.h"

namespace Chaos
{
template<class T, int d>
class TPBDRigidsEvolution;

template<class T, int d>
class TPBDRigidParticles : public TRigidParticles<T, d>
{
	friend class TPBDRigidsEvolution<T, d>;

  public:
    using TRigidParticles<T, d>::Sleeping;

	TPBDRigidParticles()
	    : TRigidParticles<T, d>()
	{
		this->MParticleType = EParticleType::Dynamic;
		TArrayCollection::AddArray(&MP);
		TArrayCollection::AddArray(&MQ);
		TArrayCollection::AddArray(&MPreV);
		TArrayCollection::AddArray(&MPreW);
	}
	TPBDRigidParticles(const TPBDRigidParticles<T, d>& Other) = delete;
	TPBDRigidParticles(TPBDRigidParticles<T, d>&& Other)
	    : TRigidParticles<T, d>(MoveTemp(Other))
		, MP(MoveTemp(Other.MP))
		, MQ(MoveTemp(Other.MQ))
		, MPreV(MoveTemp(Other.MPreV))
		, MPreW(MoveTemp(Other.MPreW))
	{
		this->MParticleType = EParticleType::Dynamic;
		TArrayCollection::AddArray(&MP);
		TArrayCollection::AddArray(&MQ);
		TArrayCollection::AddArray(&MPreV);
		TArrayCollection::AddArray(&MPreW);
	}

	const TVector<T, d>& P(const int32 index) const { return MP[index]; }
	TVector<T, d>& P(const int32 index) { return MP[index]; }

	const TRotation<T, d>& Q(const int32 index) const { return MQ[index]; }
	TRotation<T, d>& Q(const int32 index) { return MQ[index]; }

	const TVector<T, d>& PreV(const int32 index) const { return MPreV[index]; }
	TVector<T, d>& PreV(const int32 index) { return MPreV[index]; }

	const TVector<T, d>& PreW(const int32 index) const { return MPreW[index]; }
	TVector<T, d>& PreW(const int32 index) { return MPreW[index]; }

	typedef TPBDRigidParticleHandle<T, d> THandleType;
	const THandleType* Handle(int32 Index) const { return static_cast<const THandleType*>(TGeometryParticles<T,d>::Handle(Index)); }

	//cannot be reference because double pointer would allow for badness, but still useful to have non const access to handle
	THandleType* Handle(int32 Index) { return static_cast<THandleType*>(TGeometryParticles<T, d>::Handle(Index)); }

	void SetSleeping(int32 Index, bool bSleeping)
	{
		if (Sleeping(Index) && bSleeping == false)
		{
			PreV(Index) = this->V(Index);
			PreW(Index) = this->W(Index);
		}

		if (this->ObjectState(Index) == EObjectStateType::Dynamic || this->ObjectState(Index) == EObjectStateType::Sleeping)
		{
			this->ObjectState(Index) = bSleeping ? EObjectStateType::Sleeping : EObjectStateType::Dynamic;
		}

		if (bSleeping)
		{
			ensure(this->ObjectState(Index) != EObjectStateType::Kinematic);
			ensure(this->ObjectState(Index) != EObjectStateType::Static);
		}
	}

	void SetObjectState(int32 Index, EObjectStateType InObjectState)
	{
		const EObjectStateType CurrentState = this->ObjectState(Index);

		if (CurrentState == EObjectStateType::Uninitialized)
		{
			// When the state is first initialized, treat it like a static.
			this->InvM(Index) = 0.0f;
			this->InvI(Index) = PMatrix<float, 3, 3>(0);
		}

		if (CurrentState == EObjectStateType::Dynamic && (InObjectState == EObjectStateType::Kinematic || InObjectState == EObjectStateType::Static))
		{
			// Transitioning from dynamic to static or kinematic, set inverse mass and inertia tensor to zero.
			this->InvM(Index) = 0.0f;
			this->InvI(Index) = PMatrix<float, 3, 3>(0);
		}
		else if ((CurrentState == EObjectStateType::Kinematic || CurrentState == EObjectStateType::Static || CurrentState == EObjectStateType::Uninitialized) && InObjectState == EObjectStateType::Dynamic)
		{
			// Transitioning from kinematic or static to dynamic, compute the inverses.
			checkSlow(this->M(Index) != 0.0);
			checkSlow(this->I(Index).M[0][0] != 0.0);
			checkSlow(this->I(Index).M[1][1] != 0.0);
			checkSlow(this->I(Index).M[2][2] != 0.0);
			this->InvM(Index) = 1.f / this->M(Index);
			this->InvI(Index) = Chaos::PMatrix<float, 3, 3>(
				1.f / this->I(Index).M[0][0], 0.f, 0.f,
				0.f, 1.f / this->I(Index).M[1][1], 0.f,
				0.f, 0.f, 1.f / this->I(Index).M[2][2]);
		}
		else if (InObjectState == EObjectStateType::Sleeping)
		{
			SetSleeping(Index, true);
			return;
		}

		this->ObjectState(Index) = InObjectState;
	}

	FString ToString(int32 index) const
	{
		FString BaseString = TRigidParticles<T, d>::ToString(index);
		return FString::Printf(TEXT("%s, MP:%s, MQ:%s, MPreV:%s, MPreW:%s"), *BaseString, *P(index).ToString(), *Q(index).ToString(), *PreV(index).ToString(), *PreW(index).ToString());
	}

	void Serialize(FChaosArchive& Ar)
	{
		TRigidParticles<T, d>::Serialize(Ar);
		Ar << MP << MQ << MPreV << MPreW;
	}

  private:
	TArrayCollectionArray<TVector<T, d>> MP;
	TArrayCollectionArray<TRotation<T, d>> MQ;
	TArrayCollectionArray<TVector<T, d>> MPreV;
	TArrayCollectionArray<TVector<T, d>> MPreW;
};

template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TPBDRigidParticles<T, d>& Particles)
{
	Particles.Serialize(Ar);
	return Ar;
}
}
