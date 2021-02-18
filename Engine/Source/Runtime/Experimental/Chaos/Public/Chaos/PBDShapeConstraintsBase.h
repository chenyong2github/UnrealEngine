// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"

#include <functional>

namespace Chaos
{
	class FPBDShapeConstraintsBase
	{
	public:
		FPBDShapeConstraintsBase(
			int32 InParticleOffset,
			int32 InParticleCount,
			const TArray<FVec3>& StartPositions,
			const TArray<FVec3>& TargetPositions,
			const FReal Stiffness
		)
			: MTargetPositions(TargetPositions)
			, MParticleOffset(InParticleOffset)
			, MStiffness(Stiffness)
		{
			const int32 NumConstraints = InParticleCount;
			MDists.SetNumUninitialized(InParticleCount);
			for (int32 Index = 0; Index < InParticleCount; ++Index)
			{
				const int32 ParticleIndex = MParticleOffset + Index;
				const FVec3& P1 = StartPositions[ParticleIndex];
				const FVec3& P2 = MTargetPositions[ParticleIndex];
				MDists[Index] = (P1 - P2).Size();
			}
		}
		virtual ~FPBDShapeConstraintsBase() {}

		FVec3 GetDelta(const FPBDParticles& InParticles, const int32 Index) const
		{
			checkSlow(Index >= MParticleOffset && Index < MParticleOffset + MDists.Num())
			if (InParticles.InvM(Index) == (FReal)0.)
			{
				return FVec3(0.);
			}
			const FVec3& P1 = InParticles.P(Index);
			const FVec3& P2 = MTargetPositions[Index];
			const FVec3 Difference = P1 - P2;
			const FReal Distance = Difference.Size();
			const FVec3 Direction = Difference / Distance;
			const FVec3 Delta = (Distance - MDists[Index - MParticleOffset]) * Direction;
			return MStiffness * Delta / InParticles.InvM(Index);
		}

	protected:
		const TArray<FVec3>& MTargetPositions;
		const int32 MParticleOffset;

	private:
		TArray<FReal> MDists;
		FReal MStiffness;
	};
}
