// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/ParticleRule.h"
#include "Chaos/DynamicParticles.h"
#include "Chaos/PBDParticles.h"

namespace Chaos
{
class FPBDTetConstraintsBase
{
  public:
	FPBDTetConstraintsBase(const FDynamicParticles& InParticles, TArray<TVec4<int32>>&& Constraints, const FReal Stiffness = (FReal)1.)
	    : MConstraints(Constraints), MStiffness(Stiffness)
	{
		for (auto Constraint : MConstraints)
		{
			const FVec3& P1 = InParticles.X(Constraint[0]);
			const FVec3& P2 = InParticles.X(Constraint[1]);
			const FVec3& P3 = InParticles.X(Constraint[2]);
			const FVec3& P4 = InParticles.X(Constraint[3]);
			MVolumes.Add(FVec3::DotProduct(FVec3::CrossProduct(P2 - P1, P3 - P1), P4 - P1) / (FReal)6.);
		}
	}
	virtual ~FPBDTetConstraintsBase() {}

	TVec4<FVec3> GetGradients(const FPBDParticles& InParticles, const int32 i) const
	{
		TVec4<FVec3> Grads;
		const auto& Constraint = MConstraints[i];
		const FVec3& P1 = InParticles.P(Constraint[0]);
		const FVec3& P2 = InParticles.P(Constraint[1]);
		const FVec3& P3 = InParticles.P(Constraint[2]);
		const FVec3& P4 = InParticles.P(Constraint[3]);
		const FVec3 P2P1 = P2 - P1;
		const FVec3 P3P1 = P3 - P1;
		const FVec3 P4P1 = P4 - P1;
		Grads[1] = FVec3::CrossProduct(P3P1, P4P1) / (FReal)6.;
		Grads[2] = FVec3::CrossProduct(P4P1, P2P1) / (FReal)6.;
		Grads[3] = FVec3::CrossProduct(P2P1, P3P1) / (FReal)6.;
		Grads[0] = -1 * (Grads[1] + Grads[2] + Grads[3]);
		return Grads;
	}

	FReal GetScalingFactor(const FPBDParticles& InParticles, const int32 i, const TVec4<FVec3>& Grads) const
	{
		const auto& Constraint = MConstraints[i];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const int32 i4 = Constraint[3];
		const FVec3& P1 = InParticles.P(i1);
		const FVec3& P2 = InParticles.P(i2);
		const FVec3& P3 = InParticles.P(i3);
		const FVec3& P4 = InParticles.P(i4);
		FReal Volume = FVec3::DotProduct(FVec3::CrossProduct(P2 - P1, P3 - P1), P4 - P1) / (FReal)6.;
		FReal S = (Volume - MVolumes[i]) / (InParticles.InvM(i1) * Grads[0].SizeSquared() +
			                            InParticles.InvM(i2) * Grads[1].SizeSquared() + 
			                            InParticles.InvM(i3) * Grads[2].SizeSquared() + 
			                            InParticles.InvM(i4) * Grads[3].SizeSquared());
		return MStiffness * S;
	}

  protected:
	TArray<TVec4<int32>> MConstraints;

  private:
	TArray<FReal> MVolumes;
	FReal MStiffness;
};

template<class T>
using PBDTetConstraintsBase UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDTetConstraintsBase instead") = FPBDTetConstraintsBase;

}
