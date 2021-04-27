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

	struct FTether
	{
		int32 Start;
		int32 End;
		FReal RefLength;

		FTether(int32 InStart, int32 InEnd, FReal InRefLength): Start(InStart), End(InEnd), RefLength(InRefLength) {}

		inline FVec3 GetDelta(const FPBDParticles& Particles) const
		{
			checkSlow(Particles.InvM(Start) == (FReal)0.);
			checkSlow(Particles.InvM(End) > (FReal)0.);
			FVec3 Direction = Particles.P(Start) - Particles.P(End);
			const FReal Length = Direction.SafeNormalize();
			const FReal Offset = Length - RefLength;
			return Offset < (FReal)0. ? FVec3((FReal)0.) : Offset * Direction;
		};

		inline void GetDelta(const FPBDParticles& Particles, FVec3& OutDirection, FReal& OutOffset) const
		{
			checkSlow(Particles.InvM(Start) == (FReal)0.);
			checkSlow(Particles.InvM(End) > (FReal)0.);
			OutDirection = Particles.P(Start) - Particles.P(End);
			const FReal Length = OutDirection.SafeNormalize();
			OutOffset = FMath::Max((FReal)0., Length - RefLength);
		};
	};

	FPBDLongRangeConstraintsBase(
		const FPBDParticles& Particles,
		const int32 InParticleOffset,
		const int32 InParticleCount,
		const TMap<int32, TSet<int32>>& PointToNeighbors,
		const TConstArrayView<FReal>& StiffnessMultipliers,
		const int32 MaxNumTetherIslands = 4,
		const FVec2& InStiffness = FVec2((FReal)0., (FReal)1.),
		const FReal LimitScale = (FReal)1,
		const EMode InMode = EMode::AccurateTetherFastLength);

	virtual ~FPBDLongRangeConstraintsBase() {}

	EMode GetMode() const { return Mode; }

	// Return the stiffness input values used by the constraint
	FVec2 GetStiffness() const { return Stiffness.GetWeightedValue(); }

	// Set the stiffness input values used by the constraint
	void SetStiffness(const FVec2& InStiffness) { Stiffness.SetWeightedValue(InStiffness); }

	// Set stiffness offset and range, as well as the simulation stiffness exponent
	void ApplyProperties(const FReal Dt, const int32 NumIterations) { Stiffness.ApplyValues(Dt, NumIterations); }

	const TArray<FTether>& GetTethers() const { return Tethers; }

	static TArray<TArray<int32>> ComputeIslands(const TMap<int32, TSet<int32>>& PointToNeighbors, const TArray<int32>& KinematicParticles);

protected:
	void ComputeEuclideanConstraints(const FPBDParticles& Particles, const TMap<int32, TSet<int32>>& PointToNeighbors, const int32 NumberOfAttachments);
	void ComputeGeodesicConstraints(const FPBDParticles& Particles, const TMap<int32, TSet<int32>>& PointToNeighbors, const int32 NumberOfAttachments);

protected:
	TArray<FTether> Tethers;
	TPBDActiveView<TArray<FTether>> TethersView;
	FPBDStiffness Stiffness;
	const EMode Mode;
	const int32 ParticleOffset;
	const int32 ParticleCount;
};
}
