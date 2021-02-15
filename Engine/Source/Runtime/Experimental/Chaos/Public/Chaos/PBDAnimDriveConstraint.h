// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ParticleRule.h"
#include "Chaos/Framework/Parallel.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainersFwd.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Anim Drive Constraint"), STAT_PBD_AnimDriveConstraint, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Chaos PBD Anim Drive Constraint Set Stiffness"), STAT_PBD_AnimDriveConstraintSetStiffness, STATGROUP_Chaos);

namespace Chaos
{
	class FPBDAnimDriveConstraint : public FParticleRule
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
			, Stiffness((FReal)0., (FReal)1.)
			, Damping((FReal)0., (FReal)1.)
		{
			static_assert(StiffnessTableSize > 0 && StiffnessTableSize < 256, "The Stiffness lookup table is restricted to uint8 sized indices");
			static_assert(DampingTableSize > 0 && DampingTableSize < 256, "The Damping lookup tables is restricted to uint8 sized indices");

			// Convert the weight maps into an array of lookup indices to the stiffness table
			auto IntializeTableIndices = [this](const TConstArrayView<FReal>& Multipliers, const int32 TableSize, TArray<uint8>& OutIndices)
			{
				if (Multipliers.Num() == ParticleCount)
				{
					OutIndices.AddUninitialized(ParticleCount);

					static const FReal TableScale = (FReal)(TableSize - 1);

					for (int32 Index = 0; Index < ParticleCount; ++Index)
					{
						OutIndices[Index] = (uint8)(FMath::Clamp(Multipliers[Index], (FReal)0., (FReal)1.) * TableScale);
					}
				}
			};

			IntializeTableIndices(StiffnessMultipliers, StiffnessTableSize, StiffnessIndices);
			IntializeTableIndices(DampingMultipliers, DampingTableSize, DampingIndices);

			StiffnessTable.AddZeroed(StiffnessTableSize);
			DampingTable.AddZeroed(DampingTableSize);
		}

		virtual ~FPBDAnimDriveConstraint() {}

		// Set stiffness offset and range, as well as the simulation stiffness exponent
		inline void ApplyProperties(const FReal Dt, const int32 NumIterations)
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_AnimDriveConstraintSetStiffness);

			// Define the stiffness mapping function
			static const FReal ParameterFitLogBase = FMath::Loge(ParameterFitBase);
			const FReal Exponent = Dt * ParameterFrequency / (FReal)NumIterations;

			auto SimulationValue = [Exponent](const FReal InValue)->FReal
			{
				// Get a very steep exponential curve between the [0, 1] range to make easier to set the parameter
				// The base has been chosen empirically
				// ParameterValue = Pow(ParameterFitBase, ParameterValue - 1)
				const FReal ParameterFit = FMath::Exp(ParameterFitLogBase * (FMath::Clamp(InValue, (FReal)0., (FReal)1.) - (FReal)1.));

				// Use simulation dependent stiffness exponent to alleviate the variations in effect when Dt and NumIterations change
				// This is based on the Position-Based Simulation Methods paper (page 8),
				// but uses the delta time in addition of the number of iterations in the calculation of the error term.
				const FReal LogValue = FMath::Loge((FReal)1. - ParameterFit);
				return (FReal)1. - FMath::Exp(LogValue * Exponent);
			};

			auto InitializeTable = [&SimulationValue](const FVec2& WeightedValue, const int32 TableSize, TArray<FReal>& OutTable)
			{
				const FReal Offset = WeightedValue[0];
				const FReal Range = WeightedValue[1] - WeightedValue[0];
				static const FReal WeightIncrement = (FReal)1. / (FReal)(TableSize - 1); // Must allow full range from 0 to 1 included
				for (int32 Index = 0; Index < TableSize; ++Index)
				{
					const FReal Weight = (FReal)Index * WeightIncrement;
					OutTable[Index] = SimulationValue(Offset + Weight * Range);
				}
			};

			InitializeTable(Stiffness, StiffnessTableSize, StiffnessTable);
			InitializeTable(Damping, DampingTableSize, DampingTable);
		}

		// Return the stiffness input values used by the constraint
		inline FVec2 GetStiffness() const { return Stiffness; }

		// Return the damping input values used by the constraint
		inline FVec2 GetDamping() const { return Damping; }

		void SetProperties(const FVec2& InStiffness, const FVec2& InDamping)
		{
			Stiffness = InStiffness.ClampAxes((FReal)0., (FReal)1.);
			Damping = InDamping.ClampAxes((FReal)0., (FReal)1.);
		}

		inline virtual void Apply(FPBDParticles& InParticles, const FReal Dt) const override
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_AnimDriveConstraint);

			if (StiffnessIndices.Num() == ParticleCount)
			{
				if (DampingIndices.Num() == ParticleCount)
				{
					PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile needed for these parallel loop based on particle count
					{
						const FReal ParticleStiffness = StiffnessTable[StiffnessIndices[Index]];
						const FReal ParticleDamping = DampingTable[DampingIndices[Index]];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
				else
				{
					const FReal ParticleDamping = DampingTable[0];
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						const FReal ParticleStiffness = StiffnessTable[StiffnessIndices[Index]];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
			}
			else
			{
				const FReal ParticleStiffness = StiffnessTable[0];
				if (DampingIndices.Num() == ParticleCount)
				{
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						const FReal ParticleDamping = DampingTable[DampingIndices[Index]];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
				else
				{
					const FReal ParticleDamping = DampingTable[0];
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
		static constexpr FReal ParameterFrequency = (FReal)120.;  // 60Hz @ 2 iterations as a root for all stiffness values TODO: Make this a solver parameter
		static constexpr FReal ParameterFitBase = (FReal)1.e3;  // Logarithm base to use in the stiffness parameter fit function
		static constexpr int32 StiffnessTableSize = 16;  // Size of the lookup table, can't be more than 256 values
		static constexpr int32 DampingTableSize = 16;  // Size of the lookup table, can't be more than 256 values

		const TArray<FVec3>& AnimationPositions;  // Use global index (needs adding ParticleOffset)
		const TArray<FVec3>& OldAnimationPositions;  // Use global index (needs adding ParticleOffset)
		const int32 ParticleOffset;
		const int32 ParticleCount;

		TArray<uint8> StiffnessIndices; // Per particle array of index to the stiffness table. Use local index
		TArray<FReal> StiffnessTable;  // Fixed lookup table of stiffness values, use uint8 indexation
		TArray<uint8> DampingIndices; // Per particle array of index to the stiffness table. Use local index
		TArray<FReal> DampingTable;  // Fixed lookup table of stiffness values, use uint8 indexation

		FVec2 Stiffness;  // Input stiffness before parameter fitting
		FVec2 Damping;  // Input damping before parameter fitting
	};

	template<typename T, int d>
	using TPBDAnimDriveConstraint UE_DEPRECATED(4.27, "Deprecated. this class is to be deleted, use FPBDAnimDriveConstraint instead") = FPBDAnimDriveConstraint;
}
