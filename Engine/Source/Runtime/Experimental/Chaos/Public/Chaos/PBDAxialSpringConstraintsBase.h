// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDParticles.h"
#include "Chaos/PBDStiffness.h"
#include "Containers/Array.h"

namespace Chaos
{

class FPBDAxialSpringConstraintsBase
{
public:
	FPBDAxialSpringConstraintsBase(
		const FPBDParticles& Particles,
		int32 ParticleOffset,
		int32 ParticleCount,
		const TArray<TVec3<int32>>& InConstraints,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const FVec2& InStiffness,
		bool bTrimKinematicConstraints)
		: Constraints(TrimConstraints(InConstraints, 
			[&Particles, bTrimKinematicConstraints](int32 Index0, int32 Index1, int32 Index2)
			{
				return bTrimKinematicConstraints && Particles.InvM(Index0) == (FReal)0. && Particles.InvM(Index1) == (FReal)0. && Particles.InvM(Index2) == (FReal)0.;
			}))
		, Stiffness(InStiffness, StiffnessMultipliers, TConstArrayView<TVec3<int32>>(Constraints), ParticleOffset, ParticleCount)
	{
		Init(Particles);
	}

	virtual ~FPBDAxialSpringConstraintsBase() {}

	void SetProperties(const FVec2& InStiffness) { Stiffness.SetWeightedValue(InStiffness); }

	void ApplyProperties(const FReal Dt, const int32 NumIterations) { Stiffness.ApplyValues(Dt, NumIterations); }

protected:
	inline FVec3 GetDelta(const FPBDParticles& Particles, const int32 ConstraintIndex, const FReal ExpStiffnessValue) const
	{
		const TVec3<int32>& Constraint = Constraints[ConstraintIndex];
		const int32 i1 = Constraint[0];
		const int32 i2 = Constraint[1];
		const int32 i3 = Constraint[2];
		const FReal PInvMass = Particles.InvM(i3) * ((FReal)1. - Barys[ConstraintIndex]) + Particles.InvM(i2) * Barys[ConstraintIndex];
		if (Particles.InvM(i1) == (FReal)0. && PInvMass == (FReal)0.)
		{
			return FVec3((FReal)0.);
		}
		const FVec3& P1 = Particles.P(i1);
		const FVec3& P2 = Particles.P(i2);
		const FVec3& P3 = Particles.P(i3);
		const FVec3 P = (P2 - P3) * Barys[ConstraintIndex] + P3;
		const FVec3 Difference = P1 - P;
		const FReal Distance = Difference.Size();
		if (UNLIKELY(Distance <= (FReal)SMALL_NUMBER))
		{
			return FVec3((FReal)0.);
		}
		const FVec3 Direction = Difference / Distance;
		const FVec3 Delta = (Distance - Dists[ConstraintIndex]) * Direction;
		const FReal CombinedInvMass = PInvMass + Particles.InvM(i1);
		checkSlow(CombinedInvMass > (FReal)1e-7);
		return ExpStiffnessValue * Delta / CombinedInvMass;
	}

private:
	FReal FindBary(const FPBDParticles& Particles, const int32 i1, const int32 i2, const int32 i3)
	{
		const FVec3& P1 = Particles.X(i1);
		const FVec3& P2 = Particles.X(i2);
		const FVec3& P3 = Particles.X(i3);
		const FVec3& P32 = P3 - P2;
		const FReal Bary = FVec3::DotProduct(P32, P3 - P1) / P32.SizeSquared();
		return FMath::Clamp(Bary, (FReal)0., (FReal)1.);
	}

	template<typename Predicate>
	TArray<TVec3<int32>> TrimConstraints(const TArray<TVec3<int32>>& InConstraints, Predicate TrimPredicate)
	{
		TSet<TVec3<int32>> TrimmedConstraints;
		TrimmedConstraints.Reserve(InConstraints.Num());

		for (const TVec3<int32>& Constraint : InConstraints)
		{
			const int32 Index0 = Constraint[0];
			const int32 Index1 = Constraint[1];
			const int32 Index2 = Constraint[2];

			if (!TrimPredicate(Index0, Index1, Index2))
			{
				TrimmedConstraints.Add(
					Index0 <= Index1 ?
						Index1 <= Index2 ? TVec3<int32>(Index0, Index1, Index2) :
						Index0 <= Index2 ? TVec3<int32>(Index0, Index2, Index1) :
										   TVec3<int32>(Index2, Index0, Index1) :
					// Index1 < Index0
						Index0 <= Index2 ? TVec3<int32>(Index1, Index0, Index2) :
						Index1 <= Index2 ? TVec3<int32>(Index1, Index2, Index0) :
										   TVec3<int32>(Index2, Index1, Index0));
			}
		}
		return TrimmedConstraints.Array();
	}

	void Init(const FPBDParticles& Particles)
	{
		Barys.Reset(Constraints.Num());
		Dists.Reset(Constraints.Num());

		for (TVec3<int32>& Constraint : Constraints)
		{
			int32 i1 = Constraint[0];
			int32 i2 = Constraint[1];
			int32 i3 = Constraint[2];
			// Find Bary closest to 0.5
			const FReal Bary1 = FindBary(Particles, i1, i2, i3);
			const FReal Bary2 = FindBary(Particles, i2, i3, i1);
			const FReal Bary3 = FindBary(Particles, i3, i1, i2);
			FReal Bary = Bary1;
			const FReal Bary1dist = FGenericPlatformMath::Abs(Bary1 - (FReal)0.5);
			const FReal Bary2dist = FGenericPlatformMath::Abs(Bary2 - (FReal)0.5);
			const FReal Bary3dist = FGenericPlatformMath::Abs(Bary3 - (FReal)0.5);
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
			const FVec3& P1 = Particles.X(i1);
			const FVec3& P2 = Particles.X(i2);
			const FVec3& P3 = Particles.X(i3);
			const FVec3 P = (P2 - P3) * Bary + P3;
			Barys.Add(Bary);
			Dists.Add((P1 - P).Size());
		}
	}

protected:
	TArray<TVec3<int32>> Constraints;
	TArray<FReal> Barys;
	TArray<FReal> Dists;
	FPBDStiffness Stiffness;
};

}
