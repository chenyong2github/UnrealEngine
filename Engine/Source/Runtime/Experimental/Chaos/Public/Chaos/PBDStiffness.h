// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Framework/Parallel.h"
#include "Containers/ArrayView.h"
#include "Containers/ContainersFwd.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos PBD Stiffness Apply Values"), STAT_PBD_StiffnessApplyValues, STATGROUP_Chaos);

namespace Chaos
{

/**
 * Stiffness class for managing real time update to the weight map and low/high value ranges
 * and to exponentiate the stiffness value depending on the iterations and Dt.
 */
class FPBDStiffness final
{
public:
	/**
	 * Weightmap particle constructor. 
	 */
	inline FPBDStiffness(
		const FVec2& InWeightedValue,
		const TConstArrayView<FRealSingle>& Multipliers = TConstArrayView<FRealSingle>(),
		int32 ParticleCount = 0,  // A value of 0 also disables the map
		int32 TableSize = 16,  // Size of the lookup table, can't be more than 256 values, the larger the table the longer it takes to apply changes to the stiffness values
		FReal InParameterFitBase = (FReal)1.e3);  // Logarithm base to use in the stiffness parameter fit function

	/**
	 * Weightmap constraint constructor. 
	 */
	template<int32 Valence>
	inline FPBDStiffness(
		const FVec2& InWeightedValue,
		const TConstArrayView<FRealSingle>& Multipliers = TConstArrayView<FRealSingle>(),
		const TConstArrayView<TVector<int32, Valence>>& Constraints = TConstArrayView<TVector<int32, Valence>>(),
		int32 ParticleOffset = INDEX_NONE, // Constraints have usually a particle offset added to them compared to the weight maps that always starts at index 0
		int32 ParticleCount = 0,  // A value of 0 also disables the map
		int32 TableSize = 16,  // Size of the lookup table, can't be more than 256 values, the larger the table the longer it takes to apply changes to the stiffness values
		FReal InParameterFitBase = (FReal)1.e3,  // Logarithm base to use in the stiffness parameter fit function
		typename TEnableIf<Valence >= 2 && Valence <= 4>::Type* = nullptr);  // Prevents incorrect valence, the value is actually unused

	~FPBDStiffness() {}

	/** Return the number of values stored in the weight map. */
	int32 Num() const { return Indices.Num(); }

	/** Return whether this object contains weight map values. */
	bool HasWeightMap() const { return Table.Num() > 1 && FMath::Abs(WeightedValue[0] - WeightedValue[1]) > KINDA_SMALL_NUMBER; }

	/**
	 * Set the low and high values of the weight map.
	 * The weight map table only gets updated after ApplyValues is called.
	 */
	void SetWeightedValue(const FVec2& InWeightedValue) { WeightedValue = InWeightedValue.ClampAxes((FReal)0., (FReal)1.); }

	/**
	 * Return the low and high values set for this weight map.
	 * Both values will always be between 0 and 1 due to having been clamped in SetWeightedValue.
	 */
	const FVec2& GetWeightedValue() const { return WeightedValue; }

	/** Update the weight map table with the current simulation parameters. */
	inline void ApplyValues(const FReal Dt, const int32 NumIterations);

	/**
	 * Lookup for the exponential weighted value at the specified weight map index.
	 * This function will assert if it is called with a non zero index on an empty weight map.
	*/
	FReal operator[](int32 Index) const { return Table[Indices[Index]]; }

	/** Return the exponential value at the Low weight. */
	FReal GetLow() const { return Table[0]; }

	/** Return the exponential value at the High weight. */
	FReal GetHigh() const { return Table.Last(); }

	/** Return the exponential stiffness value when the weight map is not used. */
	explicit operator FReal() const { return GetLow(); }

private:
	static constexpr FReal ParameterFrequency = (FReal)120.;  // 60Hz @ 2 iterations as a root for all stiffness values TODO: Make this a global solver parameter

	TArray<uint8> Indices; // Per particle/constraints array of index to the stiffness table
	TArray<FReal> Table;  // Fixed lookup table of stiffness values, use uint8 indexation
	FVec2 WeightedValue;
	const FReal ParameterFitLogBase;
};

FPBDStiffness::FPBDStiffness(
	const FVec2& InWeightedValue,
	const TConstArrayView<FRealSingle>& Multipliers,
	int32 ParticleCount,
	int32 TableSize,
	FReal InParameterFitBase)
	: WeightedValue(InWeightedValue.ClampAxes((FReal)0., (FReal)1.))
	, ParameterFitLogBase(FMath::Loge(InParameterFitBase))
{
	check(TableSize > 0 && TableSize < 256);  // The Stiffness lookup table is restricted to uint8 sized indices

	if (Multipliers.Num() == ParticleCount && ParticleCount > 0)
	{
		// Convert the weight maps into an array of lookup indices to the stiffness table
		Indices.AddUninitialized(ParticleCount);

		const FRealSingle TableScale = (FRealSingle)(TableSize - 1);

		for (int32 Index = 0; Index < ParticleCount; ++Index)
		{
			Indices[Index] = (uint8)(FMath::Clamp(Multipliers[Index], (FRealSingle)0., (FRealSingle)1.) * TableScale);
		}

		// Initialize empty table until ApplyValues is called
		Table.AddZeroed(TableSize);
	}
	else
	{
		// Initialize with a one element table until ApplyValues is called
		Indices.AddZeroed(1);
		Table.AddZeroed(1);
	}
}

template<int32 Valence>
FPBDStiffness::FPBDStiffness(
	const FVec2& InWeightedValue,
	const TConstArrayView<FRealSingle>& Multipliers,
	const TConstArrayView<TVector<int32, Valence>>& Constraints,
	int32 ParticleOffset,
	int32 ParticleCount,
	int32 TableSize,
	FReal InParameterFitBase,
	typename TEnableIf<Valence >= 2 && Valence <= 4>::Type*)
	: WeightedValue(InWeightedValue.ClampAxes((FReal)0., (FReal)1.))
	, ParameterFitLogBase(FMath::Loge(InParameterFitBase))
{
	check(TableSize > 0 && TableSize < 256);  // The Stiffness lookup table is restricted to uint8 sized indices

	const int32 ConstraintCount = Constraints.Num();

	if (Multipliers.Num() == ParticleCount && ParticleCount > 0 && ConstraintCount > 0)
	{
		// Convert the weight maps into an array of lookup indices to the stiffness table
		Indices.AddUninitialized(ConstraintCount);

		const FRealSingle TableScale = (FRealSingle)(TableSize - 1);

		for (int32 ConstraintIndex = 0; ConstraintIndex < ConstraintCount; ++ConstraintIndex)
		{
			const TVector<int32, Valence>& Constraint = Constraints[ConstraintIndex];

			FRealSingle Weight = 0.f;
			for (int32 Index = 0; Index < Valence; ++Index)
			{
				Weight += FMath::Clamp(Multipliers[Constraint[Index] - ParticleOffset], (FRealSingle)0., (FRealSingle)1.);
			}
			Weight /= (FRealSingle)Valence;

			Indices[ConstraintIndex] = (uint8)(Weight * TableScale);
		}

		// Initialize empty table until ApplyValues is called
		Table.AddZeroed(TableSize);
	}
	else
	{
		// Initialize with a one element table until ApplyValues is called
		Indices.AddZeroed(1);
		Table.AddZeroed(1);
	}
}


void FPBDStiffness::ApplyValues(const FReal Dt, const int32 NumIterations)
{
	SCOPE_CYCLE_COUNTER(STAT_PBD_StiffnessApplyValues);

	// Calculate the simulation exponent
	const FReal Exponent = Dt * ParameterFrequency / (FReal)NumIterations;

	// Define the stiffness mapping function
	auto SimulationValue = [this, Exponent](const FReal InValue)->FReal
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

	const FReal Offset = WeightedValue[0];
	const FReal Range = WeightedValue[1] - WeightedValue[0];
	const int32 TableSize = Table.Num();
	const FReal WeightIncrement = (TableSize > 1) ? (FReal)1. / (FReal)(TableSize - 1) : (FReal)1.; // Must allow full range from 0 to 1 included
	for (int32 Index = 0; Index < TableSize; ++Index)
	{
		const FReal Weight = (FReal)Index * WeightIncrement;
		Table[Index] = SimulationValue(Offset + Weight * Range);
	}
}

}
