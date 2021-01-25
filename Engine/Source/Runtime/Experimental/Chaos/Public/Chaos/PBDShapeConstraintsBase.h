// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"

#include <functional>

namespace Chaos
{
	template<class T, int d>
	class TPBDShapeConstraintsBase
	{
	public:
		TPBDShapeConstraintsBase(
			int32 InParticleOffset,
			int32 InParticleCount,
			const TArray<TVector<T, 3>>& StartPositions,
			const TArray<TVector<T, 3>>& TargetPositions,
			const T Stiffness
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
				const TVector<T, d>& P1 = StartPositions[ParticleIndex];
				const TVector<T, d>& P2 = MTargetPositions[ParticleIndex];
				MDists[Index] = (P1 - P2).Size();
			}
		}
		virtual ~TPBDShapeConstraintsBase() {}

		TVector<T, d> GetDelta(const TPBDParticles<T, d>& InParticles, const int32 Index) const
		{
			checkSlow(Index >= MParticleOffset && Index < MParticleOffset + MDists.Num())
			if (InParticles.InvM(Index) == (T)0.)
			{
				return TVector<T, d>(0.);
			}
			const TVector<T, d>& P1 = InParticles.P(Index);
			const TVector<T, d>& P2 = MTargetPositions[Index];
			const TVector<T, d> Difference = P1 - P2;
			const T Distance = Difference.Size();
			const TVector<T, d> Direction = Difference / Distance;
			const TVector<T, d> Delta = (Distance - MDists[Index - MParticleOffset]) * Direction;
			return MStiffness * Delta / InParticles.InvM(Index);
		}

	protected:
		const TArray<TVector<T, 3>>& MTargetPositions;
		const int32 MParticleOffset;

	private:
		TArray<T> MDists;
		T MStiffness;
	};
}
