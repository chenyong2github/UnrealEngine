// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/ParticleRule.h"

namespace Chaos
{
template<class T>
class PBDTetConstraintsBase
{
  public:
	PBDTetConstraintsBase(const TDynamicParticles<T, 3>& InParticles, TArray<TVec4<int32>>&& Constraints, const T Stiffness = (T)1)
	    : MConstraints(Constraints), MStiffness(Stiffness)
	{
		for (auto Constraint : MConstraints)
		{
			const TVec3<T>& P1 = InParticles.X(Constraint[0]);
			const TVec3<T>& P2 = InParticles.X(Constraint[1]);
			const TVec3<T>& P3 = InParticles.X(Constraint[2]);
			const TVec3<T>& P4 = InParticles.X(Constraint[3]);
			MVolumes.Add(TVec3<T>::DotProduct(TVec3<T>::CrossProduct(P2 - P1, P3 - P1), P4 - P1) / (T)6);
		}
	}
	virtual ~PBDTetConstraintsBase() {}

	TVec4<TVec3<T>> GetGradients(const TPBDParticles<T, 3>& InParticles, const int32 i) const
	{
		TVec4<TVec3<T>> Grads;
		const auto& Constraint = MConstraints[i];
		const TVec3<T>& P1 = InParticles.P(Constraint[0]);
		const TVec3<T>& P2 = InParticles.P(Constraint[1]);
		const TVec3<T>& P3 = InParticles.P(Constraint[2]);
		const TVec3<T>& P4 = InParticles.P(Constraint[3]);
		const TVec3<T> P2P1 = P2 - P1;
		const TVec3<T> P3P1 = P3 - P1;
		const TVec3<T> P4P1 = P4 - P1;
		Grads[1] = TVec3<T>::CrossProduct(P3P1, P4P1) / (T)6;
		Grads[2] = TVec3<T>::CrossProduct(P4P1, P2P1) / (T)6;
		Grads[3] = TVec3<T>::CrossProduct(P2P1, P3P1) / (T)6;
		Grads[0] = -1 * (Grads[1] + Grads[2] + Grads[3]);
		return Grads;
	}

	T GetScalingFactor(const TPBDParticles<T, 3>& InParticles, const int32 i, const TVec4<TVec3<T>>& Grads) const
	{
		const auto& Constraint = MConstraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const TVec3<T>& P1 = InParticles.P(i1);
		const TVec3<T>& P2 = InParticles.P(i2);
		const TVec3<T>& P3 = InParticles.P(i3);
		const TVec3<T>& P4 = InParticles.P(i4);
		T Volume = TVec3<T>::DotProduct(TVec3<T>::CrossProduct(P2 - P1, P3 - P1), P4 - P1) / (T)6;
		T S = (Volume - MVolumes[i]) / (InParticles.InvM(i1) * Grads[0].SizeSquared() + 
			                            InParticles.InvM(i2) * Grads[1].SizeSquared() + 
			                            InParticles.InvM(i3) * Grads[2].SizeSquared() + 
			                            InParticles.InvM(i4) * Grads[3].SizeSquared());
		return MStiffness * S;
	}

  protected:
	TArray<TVec4<int32>> MConstraints;

  private:
	TArray<T> MVolumes;
	T MStiffness;
};
}
