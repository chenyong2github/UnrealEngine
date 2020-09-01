// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleRule.h"
#include "Chaos/Framework/Parallel.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainersFwd.h"

namespace Chaos
{
	template<typename T, int d>
	class TPBDAnimDriveConstraint : public TParticleRule<T, d>
	{
	public:
		TPBDAnimDriveConstraint(
			const int32 InParticleOffset,
			const int32 InParticleCount,
			const TArray<TVector<T, d>>& InAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TConstArrayView<T>& InSpringStiffnessMultiplier  // Use local indexation
		)
			: AnimationPositions(InAnimationPositions)
			, SpringStiffnessMultiplier(InSpringStiffnessMultiplier)
			, ParticleOffset(InParticleOffset)
			, SpringStiffness((T)1.)
		{
			check(InSpringStiffnessMultiplier.Num() == InParticleCount);
		}
		virtual ~TPBDAnimDriveConstraint() {}

		inline virtual void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const override
		{
			const int32 ParticleCount = SpringStiffnessMultiplier.Num();
			PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile need for parallel loop based on particle count
			{
				ApplyAnimDriveConstraint(InParticles, Dt, Index);
			});
		}

		inline void ApplyAnimDriveConstraint(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
		{
			const int32 ParticleIndex = ParticleOffset + Index;
			if (InParticles.InvM(ParticleIndex) == (T)0.)
			{
				return;
			}

			const TVector<T, d>& NeutralPosition = AnimationPositions[ParticleIndex];
			const TVector<T, d>& ParticlePosition = InParticles.P(ParticleIndex);

			const T RelaxationFactor = FMath::Min(SpringStiffness * SpringStiffnessMultiplier[Index], (T)1.);
			InParticles.P(ParticleIndex) = RelaxationFactor * NeutralPosition + ((T)1. - RelaxationFactor) * ParticlePosition;
		}

		inline void SetSpringStiffness(T InSpringStiffness)
		{
			SpringStiffness = FMath::Clamp(InSpringStiffness, (T)0., (T)1.);
		}

		inline T GetSpringStiffness() const
		{
			return SpringStiffness;
		}

	private:
		const TArray<TVector<T, d>>& AnimationPositions;  // Use global index (needs adding ParticleOffset)
		const TConstArrayView<T> SpringStiffnessMultiplier; // Use local index
		const int32 ParticleOffset;
		T SpringStiffness;
	};
}
