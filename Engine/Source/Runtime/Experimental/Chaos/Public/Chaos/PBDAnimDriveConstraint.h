// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Framework/Parallel.h"
#include "Chaos/PBDStiffness.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Anim Drive Constraint"), STAT_PBD_AnimDriveConstraint, STATGROUP_Chaos);

namespace Chaos
{
	class FPBDAnimDriveConstraint final
	{
	public:
		FPBDAnimDriveConstraint(
			const int32 InParticleOffset,
			const int32 InParticleCount,
			const TArray<FVec3>& InAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TArray<FVec3>& InOldAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TConstArrayView<FReal>& StiffnessMultipliers,  // Use local indexation
			const TConstArrayView<FReal>& DampingMultipliers  // Use local indexation
		)
			: AnimationPositions(InAnimationPositions)
			, OldAnimationPositions(InOldAnimationPositions)
			, ParticleOffset(InParticleOffset)
			, ParticleCount(InParticleCount)
			, Stiffness(StiffnessMultipliers, FVec2((FReal)0., (FReal)1.), InParticleCount)
			, Damping(DampingMultipliers, FVec2((FReal)0., (FReal)1.), InParticleCount)
		{
		}

		~FPBDAnimDriveConstraint() {}

		// Return the stiffness input values used by the constraint
		FVec2 GetStiffness() const { return Stiffness.GetWeightedValue(); }

		// Return the damping input values used by the constraint
		FVec2 GetDamping() const { return Damping.GetWeightedValue(); }

		inline void SetProperties(const FVec2& InStiffness, const FVec2& InDamping)
		{
			Stiffness.SetWeightedValue(InStiffness);
			Damping.SetWeightedValue(InDamping);
		}

		// Set stiffness offset and range, as well as the simulation stiffness exponent
		inline void ApplyProperties(const FReal Dt, const int32 NumIterations)
		{
			Stiffness.ApplyValues(Dt, NumIterations);
			Damping.ApplyValues(Dt, NumIterations);
		}

		inline void Apply(FPBDParticles& InParticles, const FReal Dt) const
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_AnimDriveConstraint);

			if (Stiffness.HasWeightMap())
			{
				if (Damping.HasWeightMap())
				{
					PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile needed for these parallel loop based on particle count
					{
						const FReal ParticleStiffness = Stiffness[Index];
						const FReal ParticleDamping = Damping[Index];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
				else
				{
					const FReal ParticleDamping = (FReal)Damping;
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						const FReal ParticleStiffness = Stiffness[Index];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
			}
			else
			{
				const FReal ParticleStiffness = (FReal)Stiffness;
				if (Damping.HasWeightMap())
				{
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						const FReal ParticleDamping = Damping[Index];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
				else
				{
					const FReal ParticleDamping = (FReal)Damping;
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
			}
		}

	private:
		inline void ApplyHelper(FPBDParticles& Particles, const FReal InStiffness, const FReal InDamping, const FReal Dt, const int32 Index) const
		{
			const int32 ParticleIndex = ParticleOffset + Index;
			if (Particles.InvM(ParticleIndex) == (FReal)0.)
			{
				return;
			}

			FVec3& ParticlePosition = Particles.P(ParticleIndex);
			const FVec3& AnimationPosition = AnimationPositions[ParticleIndex];
			const FVec3& OldAnimationPosition = OldAnimationPositions[ParticleIndex];

			const FVec3 ParticleDisplacement = ParticlePosition - Particles.X(ParticleIndex);
			const FVec3 AnimationDisplacement = OldAnimationPosition - AnimationPosition;
			const FVec3 RelativeDisplacement = ParticleDisplacement - AnimationDisplacement;

			ParticlePosition -= InStiffness * (ParticlePosition - AnimationPosition) + InDamping * RelativeDisplacement;
		}

	private:
		const TArray<FVec3>& AnimationPositions;  // Use global index (needs adding ParticleOffset)
		const TArray<FVec3>& OldAnimationPositions;  // Use global index (needs adding ParticleOffset)
		const int32 ParticleOffset;
		const int32 ParticleCount;

		FPBDStiffness Stiffness;
		FPBDStiffness Damping;
	};

	template<typename T, int d>
	using TPBDAnimDriveConstraint UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDAnimDriveConstraint instead") = FPBDAnimDriveConstraint;
}
