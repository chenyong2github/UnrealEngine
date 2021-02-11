// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

namespace Chaos
{
class CHAOS_API FPBDLongRangeConstraintsBase
{
public:
	enum class EMode : uint8
	{
		FastTetherFastLength,
		AccurateTetherFastLength,
		AccurateTetherAccurateLength,
	};

	FPBDLongRangeConstraintsBase(
		const TDynamicParticles<FReal, 3>& InParticles,
		const TMap<int32, TSet<uint32>>& PointToNeighbors,
		const int32 NumberOfAttachments = 1,
		const FReal Stiffness = (FReal)1.,
		const FReal LimitScale = (FReal)1.,
		const EMode Mode = EMode::AccurateTetherFastLength);

	virtual ~FPBDLongRangeConstraintsBase() {}

	EMode GetMode() const { return MMode; }

	const TArray<TVec2<uint32>>& GetEuclideanConstraints() const { return MEuclideanConstraints; }
	const TArray<TArray<uint32>>& GetGeodesicConstraints() const { return MGeodesicConstraints; }

	const TArray<FReal>& GetDists() const { return MDists; }

	static TArray<TArray<uint32>> ComputeIslands(const TMap<int32, TSet<uint32>>& PointToNeighbors, const TArray<uint32>& KinematicParticles);

protected:
	template<class TConstraintType>
	inline FVec3 GetDelta(const TConstraintType& Constraint, const FPBDParticles& InParticles, const FReal RefDist) const
	{
		checkSlow(Constraint.Num() > 1);
		const uint32 i2 = Constraint[Constraint.Num() - 1];
		const uint32 i2m1 = Constraint[Constraint.Num() - 2];
		checkSlow(InParticles.InvM(Constraint[0]) == (FReal)0.);
		checkSlow(InParticles.InvM(i2) > (FReal)0.);
		const FReal Distance = ComputeGeodesicDistance(InParticles, Constraint); // This function is used for either Euclidean or Geodesic distances
		if (Distance < RefDist)
		{
			return FVec3((FReal)0.);
		}

		//const FVec3 Direction = (InParticles.P(i2m1) - InParticles.P(i2)).GetSafeNormal();
		FVec3 Direction = InParticles.P(i2m1) - InParticles.P(i2);
		const FReal DirLen = Direction.SafeNormalize();

		const FReal Offset = Distance - RefDist;
		const FVec3 Delta = MStiffness * Offset * Direction;

	/*  // ryan - this currently fails:

		const FReal NewDirLen = (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size();
		//FReal Correction = (InParticles.P(i2) - InParticles.P(i2m1)).Size() - (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size();
		const FReal Correction = DirLen - NewDirLen;
		check(Correction >= 0);

		//FReal NewDist = (Distance - (InParticles.P(i2) - InParticles.P(i2m1)).Size() + (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size());
		const FReal NewDist = Distance - DirLen + NewDirLen;
		check(FGenericPlatformMath::Abs(NewDist - RefDist) < 1e-4);
	*/
		return Delta;
	};

	static FReal ComputeGeodesicDistance(const FParticles& InParticles, const TArray<uint32>& Path)
	{
		FReal distance = 0;
		for (int32 i = 0; i < Path.Num() - 1; ++i)
		{
			distance += (InParticles.X(Path[i]) - InParticles.X(Path[i + 1])).Size();
		}
		return distance;
	}
	static FReal ComputeGeodesicDistance(const FPBDParticles& InParticles, const TArray<uint32>& Path)
	{
		FReal distance = 0;
		for (int32 i = 0; i < Path.Num() - 1; ++i)
		{
			distance += (InParticles.P(Path[i]) - InParticles.P(Path[i + 1])).Size();
		}
		return distance;
	}

	void ComputeEuclideanConstraints(const TDynamicParticles<FReal, 3>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments);
	void ComputeGeodesicConstraints(const TDynamicParticles<FReal, 3>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments);

	static FReal ComputeDistance(const FParticles& InParticles, const uint32 i, const uint32 j) { return (InParticles.X(i) - InParticles.X(j)).Size(); }
	static FReal ComputeDistance(const FPBDParticles& InParticles, const uint32 i, const uint32 j) { return (InParticles.P(i) - InParticles.P(j)).Size(); }

	static FReal ComputeGeodesicDistance(const FParticles& InParticles, const TVector<uint32, 2>& Path)
	{
		return (InParticles.X(Path[0]) - InParticles.X(Path[1])).Size();
	}
	static FReal ComputeGeodesicDistance(const FPBDParticles& InParticles, const TVector<uint32, 2>& Path)
	{
		return (InParticles.P(Path[0]) - InParticles.P(Path[1])).Size();
	}

protected:
	TArray<TVec2<uint32>> MEuclideanConstraints;
	TArray<TArray<uint32>> MGeodesicConstraints;
	TArray<FReal> MDists;
	FReal MStiffness;
	EMode MMode;
};
}
