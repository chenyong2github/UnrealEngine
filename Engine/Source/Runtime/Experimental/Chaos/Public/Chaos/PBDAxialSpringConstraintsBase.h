// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PerParticleRule.h"

#include <cmath>
#include <functional>

namespace Chaos
{
class FPBDAxialSpringConstraintsBase
{
public:
	FPBDAxialSpringConstraintsBase(const FDynamicParticles& InParticles, TArray<TVec3<int32>>&& Constraints, const FReal Stiffness = (FReal)1.)
	    : MConstraints(MoveTemp(Constraints)), MStiffness(Stiffness)
	{
		for (auto& Constraint : MConstraints)
		{
			int32 i1 = Constraint[0];
			int32 i2 = Constraint[1];
			int32 i3 = Constraint[2];
			// Find Bary closest to 0.5
			FReal Bary1 = FindBary(InParticles, i1, i2, i3);
			FReal Bary2 = FindBary(InParticles, i2, i3, i1);
			FReal Bary3 = FindBary(InParticles, i3, i1, i2);
			FReal Bary = Bary1;
			FReal Bary1dist = FGenericPlatformMath::Abs(Bary1 - 0.5);
			FReal Bary2dist = FGenericPlatformMath::Abs(Bary2 - 0.5);
			FReal Bary3dist = FGenericPlatformMath::Abs(Bary3 - 0.5);
			if (Bary3dist < Bary2dist && Bary3dist < Bary1dist)
			{
				Constraint[0] = i3;
				Constraint[1] = i1;
				Constraint[2] = i2;
				Bary = Bary3;
			}
			else if (Bary2dist < Bary1dist && Bary2dist < Bary3dist)
			{
				Constraint[0] = i2;
				Constraint[1] = i3;
				Constraint[2] = i1;
				Bary = Bary2;
			}
			// Reset as they may have changed
			i1 = Constraint[0];
			i2 = Constraint[1];
			i3 = Constraint[2];
			const FVec3& P1 = InParticles.X(i1);
			const FVec3& P2 = InParticles.X(i2);
			const FVec3& P3 = InParticles.X(i3);
			const FVec3 P = (P2 - P3) * Bary + P3;
			MBarys.Add(Bary);
			MDists.Add((P1 - P).Size());
		}
	}
	virtual ~FPBDAxialSpringConstraintsBase() {}

	inline FVec3 GetDelta(const FPBDParticles& InParticles, const int32 i) const
	{
		const auto& Constraint = MConstraints[i];
		int32 i1 = Constraint[0];
		int32 i2 = Constraint[1];
		int32 i3 = Constraint[2];
		FReal PInvMass = InParticles.InvM(i3) * (1 - MBarys[i]) + InParticles.InvM(i2) * MBarys[i];
		if (InParticles.InvM(i1) == 0 && PInvMass == 0)
			return FVec3(0);
		const FVec3& P1 = InParticles.P(i1);
		const FVec3& P2 = InParticles.P(i2);
		const FVec3& P3 = InParticles.P(i3);
		const FVec3 P = (P2 - P3) * MBarys[i] + P3;
		FVec3 Difference = P1 - P;
		FReal Distance = Difference.Size();
		if (UNLIKELY(Distance <= SMALL_NUMBER))
			return FVec3((FReal) 0);
		FVec3 Direction = Difference / Distance;
		FVec3 Delta = (Distance - MDists[i]) * Direction;
		FReal CombinedInvMass = PInvMass + InParticles.InvM(i1);
		ensure(CombinedInvMass > 1e-7);
		return MStiffness * Delta / CombinedInvMass;
	}

	void SetStiffness(FReal InStiffness) { MStiffness = FMath::Clamp(InStiffness, (FReal)0., (FReal)1.); }

private:
	  FReal FindBary(const FDynamicParticles& InParticles, const int32 i1, const int32 i2, const int32 i3)
	{
		const FVec3& P1 = InParticles.X(i1);
		const FVec3& P2 = InParticles.X(i2);
		const FVec3& P3 = InParticles.X(i3);
		const FVec3& P32 = P3 - P2;
		FReal Bary = FVec3::DotProduct(P32, P3 - P1) / P32.SizeSquared();
		if (Bary > (FReal)1.)
			Bary = (FReal)1.;
		if (Bary < (FReal)0.)
			Bary = (FReal)0.;
		return Bary;
	}

protected:
	TArray<TVec3<int32>> MConstraints;
	TArray<FReal> MBarys;
	TArray<FReal> MDists;
	FReal MStiffness;
};
}
