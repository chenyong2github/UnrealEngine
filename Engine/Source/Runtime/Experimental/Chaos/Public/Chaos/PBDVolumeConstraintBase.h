// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/ParticleRule.h"

namespace Chaos
{
template<class T>
class TPBDVolumeConstraintBase
{
  public:
	TPBDVolumeConstraintBase(const TDynamicParticles<T, 3>& InParticles, TArray<TVec3<int32>>&& constraints, const T stiffness = (T)1)
	    : MConstraints(constraints), Stiffness(stiffness)
	{
		TVec3<T> Com = TVec3<T>(0, 0, 0);
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Com += InParticles.X(i);
		}
		Com /= InParticles.Size();
		MVolume = 0;
		for (auto constraint : MConstraints)
		{
			const TVec3<T>& P1 = InParticles.X(constraint[0]);
			const TVec3<T>& P2 = InParticles.X(constraint[1]);
			const TVec3<T>& P3 = InParticles.X(constraint[2]);
			MVolume += GetVolume(P1, P2, P3, Com);
		}
		MVolume /= (T)9;
	}
	virtual ~TPBDVolumeConstraintBase() {}

	TArray<T> GetWeights(const TPBDParticles<T, 3>& InParticles, const T Alpha) const
	{
		TArray<T> W;
		W.SetNum(InParticles.Size());
		T oneminusAlpha = 1 - Alpha;
		T Wg = (T)1 / (T)InParticles.Size();
		T WlDenom = 0;
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			WlDenom += (InParticles.P(i) - InParticles.X(i)).Size();
		}
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			T Wl = (InParticles.P(i) - InParticles.X(i)).Size() / WlDenom;
			W[i] = oneminusAlpha * Wl + Alpha * Wg;
		}
		return W;
	}

	TArray<TVec3<T>> GetGradients(const TPBDParticles<T, 3>& InParticles) const
	{
		TVec3<T> Com = TVec3<T>(0, 0, 0);
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Com += InParticles.P(i);
		}
		Com /= InParticles.Size();
		TArray<TVec3<T>> Grads;
		Grads.SetNum(InParticles.Size());
		for (auto& Elem : Grads)
		{
			Elem = TVec3<T>(0, 0, 0);
		}
		for (int32 i = 0; i < MConstraints.Num(); ++i)
		{
			auto constraint = MConstraints[i];
			const int32 i1 = constraint[0];
			const int32 i2 = constraint[1];
			const int32 i3 = constraint[2];
			const TVec3<T>& P1 = InParticles.P(i1);
			const TVec3<T>& P2 = InParticles.P(i2);
			const TVec3<T>& P3 = InParticles.P(i3);
			auto area = GetArea(P1, P2, P3);
			auto Normal = GetNormal(P1, P2, P3, Com);
			Grads[i1] += area * Normal;
			Grads[i2] += area * Normal;
			Grads[i3] += area * Normal;
		}
		for (auto& Elem : Grads)
		{
			Elem *= (T)1 / (T)3;
		}
		return Grads;
	}

	T GetScalingFactor(const TPBDParticles<T, 3>& InParticles, const TArray<TVec3<T>>& Grads, const TArray<T>& W) const
	{
		TVec3<T> Com = TVec3<T>(0, 0, 0);
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Com += InParticles.P(i);
		}
		Com /= InParticles.Size();
		T Volume = 0;
		for (auto constraint : MConstraints)
		{
			const TVec3<T>& P1 = InParticles.P(constraint[0]);
			const TVec3<T>& P2 = InParticles.P(constraint[1]);
			const TVec3<T>& P3 = InParticles.P(constraint[2]);
			Volume += GetVolume(P1, P2, P3, Com);
		}
		Volume /= (T)9;
		T Denom = 0;
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Denom += W[i] * Grads[i].SizeSquared();
		}
		T S = (Volume - MVolume) / Denom;
		return Stiffness * S;
	}

  protected:
	TArray<TVec3<int32>> MConstraints;

  private:
	// Utility functions for the triangle concept
	TVec3<T> GetNormal(const TVec3<T> P1, const TVec3<T>& P2, const TVec3<T>& P3, const TVec3<T>& Com) const
	{
		auto Normal = TVec3<T>::CrossProduct(P2 - P1, P3 - P1).GetSafeNormal();
		if (TVec3<T>::DotProduct((P1 + P2 + P3) / (T)3 - Com, Normal) < 0)
			return -Normal;
		return Normal;
	}

	T GetArea(const TVec3<T>& P1, const TVec3<T>& P2, const TVec3<T>& P3) const
	{
		TVec3<T> B = (P2 - P1).GetSafeNormal();
		TVec3<T> H = TVec3<T>::DotProduct(B, P3 - P1) * B + P1;
		return (T)0.5 * (P2 - P1).Size() * (P3 - H).Size();
	}

	T GetVolume(const TVec3<T>& P1, const TVec3<T>& P2, const TVec3<T>& P3, const TVec3<T>& Com) const
	{
		return GetArea(P1, P2, P3) * TVec3<T>::DotProduct(P1 + P2 + P3, GetNormal(P1, P2, P3, Com));
	}

	T MVolume;
	T Stiffness;
};
}
