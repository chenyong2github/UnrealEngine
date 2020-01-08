// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/Map.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDShapeConstraintsBase.h"

namespace Chaos
{
	template<class T, int d>
	class TPBDShapeConstraints : public TPBDShapeConstraintsBase<T, d>
	{
		typedef TPBDShapeConstraintsBase<T, d> Base;

	public:

		TPBDShapeConstraints(const TDynamicParticles<T, d>& InParticles, uint32 InParticleIndexOffset, uint32 InParticleCount, const TArray<TVector<float, 3>>& TargetPositions, const T Stiffness = (T)1)
			: Base(InParticles, TargetPositions, Stiffness), ParticleIndexOffset(InParticleIndexOffset), ParticleCount(InParticleCount)
		{
		}
		virtual ~TPBDShapeConstraints() {}

		void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
		{
			if (InParticles.InvM(Index) > 0)
			{
				InParticles.P(Index) -= InParticles.InvM(Index) * Base::GetDelta(InParticles, Index);
			}
		}

		void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const
		{
			for (uint32 Index = ParticleIndexOffset; Index < ParticleIndexOffset + ParticleCount; Index++)
			{
				Apply(InParticles, Dt, Index);
			}
		}

	private:
		uint32 ParticleIndexOffset;
		uint32 ParticleCount;
	};
}
