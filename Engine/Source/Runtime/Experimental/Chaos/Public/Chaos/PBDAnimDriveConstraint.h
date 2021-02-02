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
	template<typename T, int d>
	class TPBDAnimDriveConstraint : public TParticleRule<T, d>
	{
	public:
		TPBDAnimDriveConstraint(
			const int32 InParticleOffset,
			const int32 InParticleCount,
			const TArray<TVector<T, d>>& InAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TArray<TVector<T, d>>& InOldAnimationPositions,  // Use global indexation (will need adding ParticleOffset)
			const TConstArrayView<T>& StiffnessMultipliers,  // Use local indexation
			const TConstArrayView<T>& DampingMultipliers  // Use local indexation
		)
			: AnimationPositions(InAnimationPositions)
			, OldAnimationPositions(InOldAnimationPositions)
			, ParticleOffset(InParticleOffset)
			, ParticleCount(InParticleCount)
			, Stiffness((T)0., (T)1.)
			, Damping((T)0., (T)1.)
		{
			static_assert(StiffnessTableSize > 0 && StiffnessTableSize < 256, "The Stiffness lookup table is restricted to uint8 sized indices");
			static_assert(DampingTableSize > 0 && DampingTableSize < 256, "The Damping lookup tables is restricted to uint8 sized indices");

			// Convert the weight maps into an array of lookup indices to the stiffness table
			auto IntializeTableIndices = [this](const TConstArrayView<T>& Multipliers, const int32 TableSize, TArray<uint8>& OutIndices)
			{
				if (Multipliers.Num() == ParticleCount)
				{
					OutIndices.AddUninitialized(ParticleCount);

					static const T TableScale = (T)(TableSize - 1);

					for (int32 Index = 0; Index < ParticleCount; ++Index)
					{
						OutIndices[Index] = (uint8)(FMath::Clamp(Multipliers[Index], (T)0., (T)1.) * TableScale);
					}
				}
			};

			IntializeTableIndices(StiffnessMultipliers, StiffnessTableSize, StiffnessIndices);
			IntializeTableIndices(DampingMultipliers, DampingTableSize, DampingIndices);

			StiffnessTable.AddZeroed(StiffnessTableSize);
			DampingTable.AddZeroed(DampingTableSize);
		}

		virtual ~TPBDAnimDriveConstraint() {}

		// Set stiffness offset and range, as well as the simulation stiffness exponent
		inline void SetProperties(const TVector<T, 2>& InStiffness, const TVector<T, 2>& InDamping, const T Dt, const int32 NumIterations)
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_AnimDriveConstraintSetStiffness);

			Stiffness = InStiffness;
			Damping = InDamping;

			// Define the stiffness mapping function
			static const T ParameterFitLogBase = FMath::Loge(ParameterFitBase);
			const T Exponent = Dt * ParameterFrequency / (T)NumIterations;

			auto SimulationValue = [Exponent](const T InValue)->T
			{
				// Get a very steep exponential curve between the [0, 1] range to make easier to set the parameter
				// The base has been chosen empirically
				// ParameterValue = Pow(ParameterFitBase, ParameterValue - 1)
				const T ParameterFit = FMath::Exp(ParameterFitLogBase * (FMath::Clamp(InValue, (T)0., (T)1.) - (T)1.));

				// Use simulation dependent stiffness exponent to alleviate the variations in effect when Dt and NumIterations change
				// This is based on the Position-Based Simulation Methods paper (page 8),
				// but uses the delta time in addition of the number of iterations in the calculation of the error term.
				const T LogValue = FMath::Loge((T)1. - ParameterFit);
				return (T)1. - FMath::Exp(LogValue * Exponent);
			};

			auto InitializeTable = [&SimulationValue](const TVector<float, 2>& WeightedValue, const int32 TableSize, TArray<T>& OutTable)
			{
				const T Offset = WeightedValue[0];
				const T Range = WeightedValue[1] - WeightedValue[0];
				static const T WeightIncrement = (T)1. / (T)(TableSize - 1); // Must allow full range from 0 to 1 included
				for (int32 Index = 0; Index < TableSize; ++Index)
				{
					const T Weight = (T)Index * WeightIncrement;
					OutTable[Index] = SimulationValue(Offset + Weight * Range);
				}
			};

			InitializeTable(Stiffness, StiffnessTableSize, StiffnessTable);
			InitializeTable(Damping, DampingTableSize, DampingTable);
		}

		// Return the stiffness input values used by the constraint
		inline TVector<T, 2> GetStiffness() const { return Stiffness; }

		// Return the damping input values used by the constraint
		inline TVector<T, 2> GetDamping() const { return Damping; }

		inline virtual void Apply(TPBDParticles<T, d>& InParticles, const T Dt) const override
		{
			SCOPE_CYCLE_COUNTER(STAT_PBD_AnimDriveConstraint);

			if (StiffnessIndices.Num() == ParticleCount)
			{
				if (DampingIndices.Num() == ParticleCount)
				{
					PhysicsParallelFor(ParticleCount, [&](int32 Index)  // TODO: profile needed for these parallel loop based on particle count
					{
						const T ParticleStiffness = StiffnessTable[StiffnessIndices[Index]];
						const T ParticleDamping = DampingTable[DampingIndices[Index]];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
				else
				{
					const T ParticleDamping = DampingTable[0];
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						const T ParticleStiffness = StiffnessTable[StiffnessIndices[Index]];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
			}
			else
			{
				const T ParticleStiffness = StiffnessTable[0];
				if (DampingIndices.Num() == ParticleCount)
				{
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						const T ParticleDamping = DampingTable[DampingIndices[Index]];
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
				else
				{
					const T ParticleDamping = DampingTable[0];
					PhysicsParallelFor(ParticleCount, [&](int32 Index)
					{
						ApplyHelper(InParticles, ParticleStiffness, ParticleDamping, Dt, Index);
					});
				}
			}
		}

	private:
		inline void ApplyHelper(TPBDParticles<T, d>& Particles, const T InStiffness, const T InDamping, const T Dt, const int32 Index) const
		{
			const int32 ParticleIndex = ParticleOffset + Index;
			if (Particles.InvM(ParticleIndex) == (T)0.)
			{
				return;
			}

			TVector<T, d>& ParticlePosition = Particles.P(ParticleIndex);
			const TVector<T, d>& AnimationPosition = AnimationPositions[ParticleIndex];
			const TVector<T, d>& OldAnimationPosition = OldAnimationPositions[ParticleIndex];

			const TVector<T, d> ParticleDisplacement = ParticlePosition - Particles.X(ParticleIndex);
			const TVector<T, d> AnimationDisplacement = OldAnimationPosition - AnimationPosition;
			const TVector<T, d> RelativeDisplacement = ParticleDisplacement - AnimationDisplacement;

			ParticlePosition -= InStiffness * (ParticlePosition - AnimationPosition) + InDamping * RelativeDisplacement;
		}

	private:
		static constexpr T ParameterFrequency = (T)120.;  // 60Hz @ 2 iterations as a root for all stiffness values TODO: Make this a solver parameter
		static constexpr T ParameterFitBase = (T)1.e3;  // Logarithm base to use in the stiffness parameter fit function
		static constexpr int32 StiffnessTableSize = 16;  // Size of the lookup table, can't be more than 256 values
		static constexpr int32 DampingTableSize = 16;  // Size of the lookup table, can't be more than 256 values

		const TArray<TVector<T, d>>& AnimationPositions;  // Use global index (needs adding ParticleOffset)
		const TArray<TVector<T, d>>& OldAnimationPositions;  // Use global index (needs adding ParticleOffset)
		const int32 ParticleOffset;
		const int32 ParticleCount;

		TArray<uint8> StiffnessIndices; // Per particle array of index to the stiffness table. Use local index
		TArray<T> StiffnessTable;  // Fixed lookup table of stiffness values, use uint8 indexation
		TArray<uint8> DampingIndices; // Per particle array of index to the stiffness table. Use local index
		TArray<T> DampingTable;  // Fixed lookup table of stiffness values, use uint8 indexation

		TVector<T, 2> Stiffness;  // Input stiffness before parameter fitting
		TVector<T, 2> Damping;  // Input damping before parameter fitting
	};
}
