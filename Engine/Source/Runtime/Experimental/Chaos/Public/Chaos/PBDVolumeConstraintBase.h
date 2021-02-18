// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/ParticleRule.h"

namespace Chaos
{
class FPBDVolumeConstraintBase
{
  public:
	  FPBDVolumeConstraintBase(const FDynamicParticles& InParticles, TArray<TVec3<int32>>&& InConstraints, const FReal InStiffness = (FReal)1.)
	    : Constraints(InConstraints), Stiffness(InStiffness)
	{
		FVec3 Com = FVec3(0, 0, 0);
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Com += InParticles.X(i);
		}
		Com /= InParticles.Size();
		RefVolume = 0;
		for (const TVec3<int32>& Constraint : Constraints)
		{
			const FVec3& P1 = InParticles.X(Constraint[0]);
			const FVec3& P2 = InParticles.X(Constraint[1]);
			const FVec3& P3 = InParticles.X(Constraint[2]);
			RefVolume += GetVolume(P1, P2, P3, Com);
		}
		RefVolume /= (FReal)9.;
	}
	virtual ~FPBDVolumeConstraintBase() {}

	TArray<FReal> GetWeights(const FPBDParticles& InParticles, const FReal Alpha) const
	{
		TArray<FReal> W;
		W.SetNum(InParticles.Size());
		FReal oneminusAlpha = (FReal)1. - Alpha;
		FReal Wg = (FReal)1. / (FReal)InParticles.Size();
		FReal WlDenom = 0;
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			WlDenom += (InParticles.P(i) - InParticles.X(i)).Size();
		}
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			FReal Wl = (InParticles.P(i) - InParticles.X(i)).Size() / WlDenom;
			W[i] = oneminusAlpha * Wl + Alpha * Wg;
		}
		return W;
	}

	TArray<FVec3> GetGradients(const FPBDParticles& InParticles) const
	{
		FVec3 Com = FVec3(0, 0, 0);
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Com += InParticles.P(i);
		}
		Com /= InParticles.Size();
		TArray<FVec3> Grads;
		Grads.SetNum(InParticles.Size());
		for (FVec3& Elem : Grads)
		{
			Elem = FVec3(0, 0, 0);
		}
		for (int32 i = 0; i < Constraints.Num(); ++i)
		{
			const TVec3<int32>& Constraint = Constraints[i];
			const int32 i1 = Constraint[0];
			const int32 i2 = Constraint[1];
			const int32 i3 = Constraint[2];
			const FVec3& P1 = InParticles.P(i1);
			const FVec3& P2 = InParticles.P(i2);
			const FVec3& P3 = InParticles.P(i3);
			const FReal Area = GetArea(P1, P2, P3);
			const FVec3 Normal = GetNormal(P1, P2, P3, Com);
			Grads[i1] += Area * Normal;
			Grads[i2] += Area * Normal;
			Grads[i3] += Area * Normal;
		}
		for (FVec3& Elem : Grads)
		{
			Elem *= (FReal)1. / (FReal)3.;
		}
		return Grads;
	}

	FReal GetScalingFactor(const FPBDParticles& InParticles, const TArray<FVec3>& Grads, const TArray<FReal>& W) const
	{
		FVec3 Com = FVec3(0, 0, 0);
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Com += InParticles.P(i);
		}
		Com /= InParticles.Size();
		FReal Volume = 0;
		for (const TVec3<int32>& Constraint : Constraints)
		{
			const FVec3& P1 = InParticles.P(Constraint[0]);
			const FVec3& P2 = InParticles.P(Constraint[1]);
			const FVec3& P3 = InParticles.P(Constraint[2]);
			Volume += GetVolume(P1, P2, P3, Com);
		}
		Volume /= (FReal)9.;
		FReal Denom = 0;
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			Denom += W[i] * Grads[i].SizeSquared();
		}
		FReal S = (Volume - RefVolume) / Denom;
		return Stiffness * S;
	}

	void SetStiffness(FReal InStiffness) { Stiffness = FMath::Clamp(InStiffness, (FReal)0., (FReal)1.); }

protected:
	TArray<TVec3<int32>> Constraints;

private:
	// Utility functions for the triangle concept
	FVec3 GetNormal(const FVec3 P1, const FVec3& P2, const FVec3& P3, const FVec3& Com) const
	{
		const FVec3 Normal = FVec3::CrossProduct(P2 - P1, P3 - P1).GetSafeNormal();
		if (FVec3::DotProduct((P1 + P2 + P3) / (FReal)3. - Com, Normal) < 0)
			return -Normal;
		return Normal;
	}

	FReal GetArea(const FVec3& P1, const FVec3& P2, const FVec3& P3) const
	{
		FVec3 B = (P2 - P1).GetSafeNormal();
		FVec3 H = FVec3::DotProduct(B, P3 - P1) * B + P1;
		return (FReal)0.5 * (P2 - P1).Size() * (P3 - H).Size();
	}

	FReal GetVolume(const FVec3& P1, const FVec3& P2, const FVec3& P3, const FVec3& Com) const
	{
		return GetArea(P1, P2, P3) * FVec3::DotProduct(P1 + P2 + P3, GetNormal(P1, P2, P3, Com));
	}

	FReal RefVolume;
	FReal Stiffness;
};
}
