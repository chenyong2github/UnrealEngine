// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDShapeConstraintsBase.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDShapeConstraints : public TPBDShapeConstraintsBase<T, d>
	{
		typedef TPBDShapeConstraintsBase<T, d> Base;
		using Base::MParticleOffset;
		using Base::MTargetPositions;

	public:

		TPBDShapeConstraints(
			int32 InParticleOffset,
			int32 InParticleCount,
			const TArray<TVector<T, 3>>& StartPositions,
			const TArray<TVector<T, 3>>& TargetPositions,
			const T Stiffness = (T)1.
		)
			: Base(InParticleOffset, InParticleCount, StartPositions, TargetPositions, Stiffness)
		{
		}
		virtual ~TPBDShapeConstraints() override {}

		void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
		{
			if (InParticles.InvM(Index) > 0)
			{
				InParticles.P(Index) -= InParticles.InvM(Index) * Base::GetDelta(InParticles, Index);
			}
		}

		void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const
		{
			for (int32 Index = MParticleOffset; Index < MParticleOffset + MTargetPositions.Num(); Index++)
			{
				Apply(InParticles, Dt, Index);
			}
		}
	};
}
