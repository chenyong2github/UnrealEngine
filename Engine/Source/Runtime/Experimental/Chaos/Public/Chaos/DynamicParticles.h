// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ArrayCollectionArray.h"
#include "Chaos/Particles.h"

namespace Chaos
{
template<class T, int d>
class TDynamicParticles : public TParticles<T, d>
{
  public:
	TDynamicParticles()
	    : TParticles<T, d>()
	{
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
	}
	TDynamicParticles(const TDynamicParticles<T, d>& Other) = delete;
	TDynamicParticles(TDynamicParticles<T, d>&& Other)
	    : TParticles<T, d>(MoveTemp(Other)), MV(MoveTemp(Other.MV)), MF(MoveTemp(Other.MF)), MM(MoveTemp(Other.MM)), MInvM(MoveTemp(Other.MInvM))
	{
		TArrayCollection::AddArray(&MV);
		TArrayCollection::AddArray(&MF);
		TArrayCollection::AddArray(&MM);
		TArrayCollection::AddArray(&MInvM);
	}

	const TVector<T, d>& V(const int32 Index) const { return MV[Index]; }
	TVector<T, d>& V(const int32 Index) { return MV[Index]; }
	const TArrayCollectionArray<TVector<T, d>>& GetV() const { return MV; }

	const TVector<T, d>& F(const int32 Index) const { return MF[Index]; }
	TVector<T, d>& F(const int32 Index) { return MF[Index]; }

	const T M(const int32 Index) const { return MM[Index]; }
	T& M(const int32 Index) { return MM[Index]; }
	const TArrayCollectionArray<T>& GetM() const { return MM; }

	const T InvM(const int32 Index) const { return MInvM[Index]; }
	T& InvM(const int32 Index) { return MInvM[Index]; }
	const TArrayCollectionArray<T>& GetInvM() const { return MInvM; }

  private:
	TArrayCollectionArray<TVector<T, d>> MV, MF;
	TArrayCollectionArray<T> MM, MInvM;
};

using FDynamicParticles = TDynamicParticles<FReal, 3>;
}
