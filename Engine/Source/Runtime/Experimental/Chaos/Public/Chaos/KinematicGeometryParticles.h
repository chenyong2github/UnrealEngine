// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/GeometryParticles.h"

namespace Chaos
{
template<class T, int d, EGeometryParticlesSimType SimType>
class TKinematicGeometryParticlesImp : public TGeometryParticlesImp<T, d, SimType>
{
  public:
	TKinematicGeometryParticlesImp()
	    : TGeometryParticlesImp<T, d, SimType>()
	{
		this->MParticleType = EParticleType::Kinematic;
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MW);
	}
	TKinematicGeometryParticlesImp(const TKinematicGeometryParticlesImp<T, d, SimType>& Other) = delete;
	TKinematicGeometryParticlesImp(TKinematicGeometryParticlesImp<T, d, SimType>&& Other)
	    : TGeometryParticlesImp<T, d, SimType>(MoveTemp(Other)), MV(MoveTemp(Other.MV)), MW(MoveTemp(Other.MW))
	{
		this->MParticleType = EParticleType::Kinematic;
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MW);
	}
	~TKinematicGeometryParticlesImp() {}

	const TVector<T, d>& V(const int32 Index) const { return MV[Index]; }
	TVector<T, d>& V(const int32 Index) { return MV[Index]; }

	const TVector<T, d>& W(const int32 Index) const { return MW[Index]; }
	TVector<T, d>& W(const int32 Index) { return MW[Index]; }

	FString ToString(int32 index) const
	{
		FString BaseString = TGeometryParticlesImp<T, d, SimType>::ToString(index);
		return FString::Printf(TEXT("%s, MV:%s, MW:%s"), *BaseString, *V(index).ToString(), *W(index).ToString());
	}

	typedef TKinematicGeometryParticleHandle<T, d> THandleType;
	const THandleType* Handle(int32 Index) const;

	//cannot be reference because double pointer would allow for badness, but still useful to have non const access to handle
	THandleType* Handle(int32 Index);

	void Serialize(FChaosArchive& Ar)
	{
		TGeometryParticlesImp<T, d, SimType>::Serialize(Ar);
		Ar << MV << MW;
	}

  private:
	TArrayCollectionArray<TVector<T, d>> MV;
	TArrayCollectionArray<TVector<T, d>> MW;
};

template <typename T, int d, EGeometryParticlesSimType SimType>
FChaosArchive& operator<<(FChaosArchive& Ar, TKinematicGeometryParticlesImp<T, d, SimType>& Particles)
{
	Particles.Serialize(Ar);
	return Ar;
}

extern template class Chaos::TKinematicGeometryParticlesImp<float, 3, Chaos::EGeometryParticlesSimType::RigidBodySim>;
extern template class Chaos::TKinematicGeometryParticlesImp<float, 3, Chaos::EGeometryParticlesSimType::Other>;

template <typename T, int d>
using TKinematicGeometryParticles = TKinematicGeometryParticlesImp<T, d, EGeometryParticlesSimType::RigidBodySim>;

template <typename T, int d>
using TKinematicGeometryClothParticles = TKinematicGeometryParticlesImp<T, d, EGeometryParticlesSimType::Other>;
}
