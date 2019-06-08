// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/GeometryParticles.h"

namespace Chaos
{
template <typename T, int d>
class TKinematicGeometryParticleHandle;

template<class T, int d>
class CHAOS_API TKinematicGeometryParticles : public TGeometryParticles<T, d>
{
  public:
	TKinematicGeometryParticles()
	    : TGeometryParticles<T, d>()
	{
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MW);
	}
	TKinematicGeometryParticles(const TKinematicGeometryParticles<T, d>& Other) = delete;
	TKinematicGeometryParticles(TKinematicGeometryParticles<T, d>&& Other)
	    : TGeometryParticles<T, d>(MoveTemp(Other)), MV(MoveTemp(Other.MV)), MW(MoveTemp(Other.MW))
	{
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MW);
	}
	~TKinematicGeometryParticles() {}

	const TVector<T, d>& V(const int32 Index) const { return MV[Index]; }
	TVector<T, d>& V(const int32 Index) { return MV[Index]; }

	const TVector<T, d>& W(const int32 Index) const { return MW[Index]; }
	TVector<T, d>& W(const int32 Index) { return MW[Index]; }

	FString ToString(int32 index) const
	{
		FString BaseString = TGeometryParticles<T, d>::ToString(index);
		return FString::Printf(TEXT("%s, MV:%s, MW:%s"), *BaseString, *V(index).ToString(), *W(index).ToString());
	}

	typedef TKinematicGeometryParticleHandle<T, d> THandleType;
	const THandleType* Handle(int32 Index) const;

	//cannot be reference because double pointer would allow for badness, but still useful to have non const access to handle
	THandleType* Handle(int32 Index);

	void Serialize(FChaosArchive& Ar)
	{
		TGeometryParticles<T, d>::Serialize(Ar);
		Ar << MV << MW;
	}

  private:
	TArrayCollectionArray<TVector<T, d>> MV;
	TArrayCollectionArray<TVector<T, d>> MW;
};

template <typename T, int d>
FChaosArchive& operator<<(FChaosArchive& Ar, TKinematicGeometryParticles<T, d>& Particles)
{
	Particles.Serialize(Ar);
	return Ar;
}
}
