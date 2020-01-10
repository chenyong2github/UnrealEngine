// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/PBDParticles.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

namespace Chaos
{
template<class T, int d>
class CHAOS_API TPBDLongRangeConstraintsBase
{
  public:
	TPBDLongRangeConstraintsBase(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments = 1, const T Stiffness = (T)1);
	virtual ~TPBDLongRangeConstraintsBase() {}

	inline TVector<T, d> GetDelta(const TPBDParticles<T, d>& InParticles, const int32 i) const
	{
		const TArray<uint32>& Constraint = MConstraints[i];
		check(Constraint.Num() > 1);
		const uint32 i1 = Constraint[0];
		const uint32 i2 = Constraint[Constraint.Num() - 1];
		const uint32 i2m1 = Constraint[Constraint.Num() - 2];
		check(InParticles.InvM(i1) == 0);
		check(InParticles.InvM(i2) > 0);
		const T Distance = ComputeGeodesicDistance(InParticles, Constraint); // This function is used for either Euclidian or Geodisic distances
		if (Distance < MDists[i])
			return TVector<T, d>(0);

		//const TVector<T, d> Direction = (InParticles.P(i2m1) - InParticles.P(i2)).GetSafeNormal();
		TVector<T, d> Direction = InParticles.P(i2m1) - InParticles.P(i2);
		const T DirLen = Direction.SafeNormalize();

		const T Offset = Distance - MDists[i];
		const TVector<T, d> Delta = MStiffness * Offset * Direction;

	/*  // ryan - this currently fails:

		const T NewDirLen = (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size();
		//T Correction = (InParticles.P(i2) - InParticles.P(i2m1)).Size() - (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size();
		const T Correction = DirLen - NewDirLen;
		check(Correction >= 0);

		//T NewDist = (Distance - (InParticles.P(i2) - InParticles.P(i2m1)).Size() + (InParticles.P(i2) + Delta - InParticles.P(i2m1)).Size());
		const T NewDist = Distance - DirLen + NewDirLen;
		check(FGenericPlatformMath::Abs(NewDist - MDists[i]) < 1e-4);
	*/
		return Delta;
	};

  protected:
	static TArray<TArray<uint32>> ComputeIslands(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors,/*const TTriangleMesh<T>& Mesh,*/ const TArray<uint32>& KinematicParticles);
	void ComputeEuclidianConstraints(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors,/*const TTriangleMesh<T>& Mesh,*/ const int32 NumberOfAttachments);
	void ComputeGeodesicConstraints(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors,/*const TTriangleMesh<T>& Mesh,*/ const int32 NumberOfAttachments);

	static T ComputeDistance(const TParticles<T, d>& InParticles, const uint32 i, const uint32 j) { return (InParticles.X(i) - InParticles.X(j)).Size(); }
	static T ComputeDistance(const TPBDParticles<T, d>& InParticles, const uint32 i, const uint32 j) { return (InParticles.P(i) - InParticles.P(j)).Size(); }
	static T ComputeGeodesicDistance(const TParticles<T, d>& InParticles, const TArray<uint32>& Path)
	{
		T distance = 0;
		for (int i = 0; i < Path.Num() - 1; ++i)
		{
			distance += (InParticles.X(Path[i]) - InParticles.X(Path[i + 1])).Size();
		}
		return distance;
	}
	static T ComputeGeodesicDistance(const TPBDParticles<T, d>& InParticles, const TArray<uint32>& Path)
	{
		T distance = 0;
		for (int i = 0; i < Path.Num() - 1; ++i)
		{
			distance += (InParticles.P(Path[i]) - InParticles.P(Path[i + 1])).Size();
		}
		return distance;
	}

  protected:
	TArray<TArray<uint32>> MConstraints;
	TArray<T> MDists;
	T MStiffness;
};
}
