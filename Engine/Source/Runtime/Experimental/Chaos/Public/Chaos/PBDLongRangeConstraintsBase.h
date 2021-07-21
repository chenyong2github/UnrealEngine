// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/PBDParticles.h"
#include "Chaos/PBDActiveView.h"
#include "Chaos/PBDStiffness.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

namespace Chaos
{
class CHAOS_API FPBDLongRangeConstraintsBase
{
public:
	enum class EMode : uint8
	{
		Euclidean,
		Geodesic,

		// Deprecated modes
		FastTetherFastLength = Euclidean,
		AccurateTetherFastLength = Geodesic,
		AccurateTetherAccurateLength = Geodesic
	};

	typedef TTuple<int32, int32, float> FTether;

	FPBDLongRangeConstraintsBase(
		const FPBDParticles& Particles,
		const int32 InParticleOffset,
		const int32 InParticleCount,
		const TArray<TConstArrayView<TTuple<int32, int32, FRealSingle>>>& InTethers,
		const TConstArrayView<FRealSingle>& StiffnessMultipliers,
		const TConstArrayView<FRealSingle>& ScaleMultipliers,
		const FVec2& InStiffness = FVec2::UnitVector,
		const FVec2& InScale = FVec2::UnitVector);

	virtual ~FPBDLongRangeConstraintsBase() {}

	// Return the stiffness input values used by the constraint
	FVec2 GetStiffness() const { return Stiffness.GetWeightedValue(); }

	// Set the stiffness input values used by the constraint
	void SetStiffness(const FVec2& InStiffness) { Stiffness.SetWeightedValue(InStiffness); }

	// Set the scale low and high value of the scale weight map
	void SetScale(const FVec2& InScale) { Scale = InScale.ClampAxes((FReal)0.01, (FReal)10.); }

	// Set stiffness offset and range, as well as the simulation stiffness exponent
	void ApplyProperties(const FReal Dt, const int32 NumIterations);

	// Return the tethers, organized in concurent friendly batches
	const TArray<TConstArrayView<FTether>>& GetTethers() const { return Tethers; }

	// Return the start index of the specified tether
	int32 GetStartIndex(const FTether& Tether) const { return Tether.Get<0>(); }

	// Return the kinematic particle index of the specified tether
	int32 GetStartParticle(const FTether& Tether) const { return GetStartIndex(Tether) + ParticleOffset; }

	// Return the end index of the specified tether
	int32 GetEndIndex(const FTether& Tether) const { return Tether.Get<1>(); }

	// Return the dynamic particle index of the specified tether
	int32 GetEndParticle(const FTether& Tether) const { return GetEndIndex(Tether) + ParticleOffset; }

	// Return the reference length of the specified tether
	FReal GetRefLength(const FTether& Tether) const { return (FReal)Tether.Get<2>(); }

	// Return the Tether scale for the specified tether
	FReal GetScale(const FTether& Tether) const { return HasScaleWeightMap() ? ScaleTable[ScaleIndices[GetEndIndex(Tether)]] : ScaleTable[0]; }

	// Return the target length of the specified tether (= RefLength * Scale)
	FReal GetTargetLength(const FTether& Tether) const { return GetRefLength(Tether) * GetScale(Tether); }

protected:
	// Return the minimum number of long range tethers in a batch to process in parallel
	static int32 GetMinParallelBatchSize();

	// Return whether the constraint has been setup with a weightmap to interpolate between two low and high values of scales
	bool HasScaleWeightMap() const { return ScaleTable.Num() > 1; }

	// Return a vector representing the amount of segment required for the tether to shrink back to its maximum target length constraint, or zero if the constraint is already met
	inline FVec3 GetDelta(const FPBDParticles& Particles, const FTether& Tether, const FReal InScale) const
	{
		const int32 Start = GetStartParticle(Tether);
		const int32 End = GetEndParticle(Tether);
		const FReal TargetLength = GetRefLength(Tether) * InScale;
		checkSlow(Particles.InvM(Start) == (FReal)0.);
		checkSlow(Particles.InvM(End) > (FReal)0.);
		FVec3 Direction = Particles.P(Start) - Particles.P(End);
		const FReal Length = Direction.SafeNormalize();
		const FReal Offset = Length - TargetLength;
		return Offset < (FReal)0. ? FVec3((FReal)0.) : Offset * Direction;
	};

	// Return a direction and length representing the amount of segment required for the tether to shrink back to its maximum target length constraint, or zero if the constraint is already met
	inline void GetDelta(const FPBDParticles& Particles, const FTether& Tether, const FReal InScale, FVec3& OutDirection, FReal& OutOffset) const
	{
		const int32 Start = GetStartParticle(Tether);
		const int32 End = GetEndParticle(Tether);
		const FReal TargetLength = GetRefLength(Tether) * InScale;
		checkSlow(Particles.InvM(Start) == (FReal)0.);
		checkSlow(Particles.InvM(End) > (FReal)0.);
		OutDirection = Particles.P(Start) - Particles.P(End);
		const FReal Length = OutDirection.SafeNormalize();
		OutOffset = FMath::Max((FReal)0., Length - TargetLength);
	};

protected:
	static constexpr int32 TableSize = 16;  // The size of the weightmaps lookup table
	const TArray<TConstArrayView<FTether>>& Tethers;  // Array view on the tether provided to this constraint
	FPBDStiffness Stiffness;  // Stiffness weightmap lookup table 
	TArray<uint8> ScaleIndices;  // Per particle array of index to the scale lookup table
	TArray<FReal> ScaleTable;  // Fixed lookup table of scale values
	FVec2 Scale;  // High and low values of tether target length scale used by the the lookup tables interpolation
	const int32 ParticleOffset;  // Index of the first usable particle
	const int32 ParticleCount;  // Number of particles available to this constraint
};
}
