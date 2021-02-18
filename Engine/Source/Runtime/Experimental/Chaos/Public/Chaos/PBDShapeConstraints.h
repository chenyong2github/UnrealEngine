// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDShapeConstraintsBase.h"

namespace Chaos
{
	class FPBDShapeConstraints : public FPBDShapeConstraintsBase
	{
		typedef FPBDShapeConstraintsBase Base;
		using Base::MParticleOffset;
		using Base::MTargetPositions;

	public:

		FPBDShapeConstraints(
			int32 InParticleOffset,
			int32 InParticleCount,
			const TArray<FVec3>& StartPositions,
			const TArray<FVec3>& TargetPositions,
			const FReal Stiffness = (FReal)1.
		)
			: Base(InParticleOffset, InParticleCount, StartPositions, TargetPositions, Stiffness)
		{
		}
		virtual ~FPBDShapeConstraints() override {}

		void Apply(FPBDParticles& InParticles, const FReal Dt, const int32 Index) const
		{
			if (InParticles.InvM(Index) > 0)
			{
				InParticles.P(Index) -= InParticles.InvM(Index) * Base::GetDelta(InParticles, Index);
			}
		}

		void Apply(FPBDParticles& InParticles, const FReal Dt) const
		{
			for (int32 Index = MParticleOffset; Index < MParticleOffset + MTargetPositions.Num(); Index++)
			{
				Apply(InParticles, Dt, Index);
			}
		}
	};
}
