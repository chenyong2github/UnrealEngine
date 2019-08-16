// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDLongRangeConstraintsBase.h"

#include "Chaos/Map.h"
#include "Chaos/Vector.h"

#include <queue>
#include <unordered_map>

using namespace Chaos;

template<class T, int d>
TPBDLongRangeConstraintsBase<T, d>::TPBDLongRangeConstraintsBase(const TDynamicParticles<T, d>& InParticles, const TMap<int32, TSet<uint32>>& PointToNeighbors, const int32 NumberOfAttachments, const T Stiffness)
    : MStiffness(Stiffness)
{
	ComputeEuclidianConstraints(InParticles, PointToNeighbors, NumberOfAttachments);
}

template<class T, int d>
TArray<TArray<uint32>> TPBDLongRangeConstraintsBase<T, d>::ComputeIslands(
    const TDynamicParticles<T, d>& InParticles,
    const TMap<int32, TSet<uint32>>& PointToNeighbors,
    const TArray<uint32>& KinematicParticles)
{
	// Compute Islands
	uint32 NextIsland = 0;
	TArray<uint32> FreeIslands;
	TArray<TArray<uint32>> IslandElements;

	TMap<uint32, uint32> ParticleToIslandMap;
	ParticleToIslandMap.Reserve(KinematicParticles.Num());

	for (const uint32 Element : KinematicParticles)
	{
		// Assign Element an island, possibly unioning existing islands
		uint32 Island = TNumericLimits<uint32>::Max();

		// KinematicParticles is generated from the keys of PointToNeighbors, so
		// we don't need to check if Element exists.
		const TSet<uint32>& Neighbors = PointToNeighbors[Element];
		for (auto Neighbor : Neighbors)
		{
			if (ParticleToIslandMap.Contains(Neighbor))
			{
				if (Island == TNumericLimits<uint32>::Max())
				{
					// We don't have an assigned island yet.  Join with the island
					// of the neighbor.
					Island = ParticleToIslandMap[Neighbor];
				}
				else
				{
					const uint32 OtherIsland = ParticleToIslandMap[Neighbor];
					if (OtherIsland != Island)
					{
						// This kinematic particle is connected to multiple islands.
						// Union them.
						for (auto OtherElement : IslandElements[OtherIsland])
						{
							check(ParticleToIslandMap[OtherElement] == OtherIsland);
							ParticleToIslandMap[OtherElement] = Island;
						}
						IslandElements[Island].Append(IslandElements[OtherIsland]);
						IslandElements[OtherIsland].Reset(); // Don't deallocate
						FreeIslands.AddUnique(OtherIsland);
					}
				}
			}
		}

		// If no connected Island was found, create a new one (or reuse an old 
		// vacated one).
		if (Island == TNumericLimits<uint32>::Max())
		{
			if (FreeIslands.Num() == 0)
			{
				Island = NextIsland++;
				IslandElements.SetNum(NextIsland);
			}
			else
			{
				// Reuse a previously allocated, but currently unused, island.
				Island = FreeIslands.Pop();
			}
		}

		ParticleToIslandMap.FindOrAdd(Element) = Island;
		check(IslandElements.IsValidIndex(Island));
		IslandElements[Island].Add(Element);
	}
	// IslandElements may contain empty arrays.
	return IslandElements;
}

template<class T, int d>
void TPBDLongRangeConstraintsBase<T, d>::ComputeEuclidianConstraints(
    const TDynamicParticles<T, d>& InParticles,
    const TMap<int32, TSet<uint32>>& PointToNeighbors,
    const int32 NumberOfAttachments)
{
	// TODO(mlentine): Support changing which particles are kinematic during simulation
	TArray<uint32> KinematicParticles;
	for (const auto& KV : PointToNeighbors)
	{
		const int32 i = KV.Key;
		if (InParticles.InvM(i) == 0.0)
		{
			KinematicParticles.Add(i);
		}
	}

	const TArray<TArray<uint32>> IslandElements = ComputeIslands(InParticles, PointToNeighbors, KinematicParticles);
	int32 NumTotalIslandElements = 0;
	for(const auto& Elements : IslandElements)
		NumTotalIslandElements += Elements.Num();
	TArray<Pair<T, uint32>> ClosestElements;
	ClosestElements.Reserve(NumTotalIslandElements);

	//FCriticalSection CriticalSection;
	//PhysicsParallelFor(InParticles.Size(), [&](int32 i)
	for (const auto& KV : PointToNeighbors)
	{
		// For each non-kinematic particle i...
		const uint32 i = KV.Key;
		if (InParticles.InvM(i) == 0.0)
			continue;

		// Measure the distance to all particles in all islands...
		ClosestElements.Reset();
		for (const TArray<uint32>& Elements : IslandElements)
		{
			// IslandElements may contain empty arrays.
			if (Elements.Num() == 0)
				continue;

			uint32 ClosestElement = TNumericLimits<uint32>::Max();
			T ClosestDistance = TNumericLimits<T>::Max();
			for (const uint32 Element : Elements)
			{
				const T Distance = ComputeDistance(InParticles, Element, i);
				if (Distance < ClosestDistance)
				{
					ClosestElement = Element;
					ClosestDistance = Distance;
				}
			}
			ClosestElements.Add(MakePair(ClosestDistance, ClosestElement));
		}

		// Order all by distance, smallest first...
		ClosestElements.Sort();

		// Take the first N-umberOfAttachments.
		if (NumberOfAttachments < ClosestElements.Num())
		{
			ClosestElements.SetNum(NumberOfAttachments);
		}

		// Add constraints between this particle, and the N closest...
		for (auto Element : ClosestElements)
		{
			//CriticalSection.Lock();
			MConstraints.Add({Element.Second, i});
			MDists.Add(Element.First);
			//CriticalSection.Unlock();
		}
	}
	//);
}

template<class T, int d>
void TPBDLongRangeConstraintsBase<T, d>::ComputeGeodesicConstraints(
    const TDynamicParticles<T, d>& InParticles,
    const TMap<int32, TSet<uint32>>& PointToNeighbors,
    const int32 NumberOfAttachments)
{
	// TODO(mlentine): Support changing which particles are kinematic during simulation
	TArray<uint32> KinematicParticles;
	for (uint32 i = 0; i < InParticles.Size(); ++i)
	{
		if (InParticles.InvM(i) == 0)
		{
			KinematicParticles.Add(i);
		}
	}
	TArray<TArray<uint32>> IslandElements = ComputeIslands(InParticles, PointToNeighbors, KinematicParticles);
	// Store distances for all adjacent vertices
	TMap<TVector<uint32, 2>, T> Distances;
	for (uint32 i = 0; i < InParticles.Size(); ++i)
	{
		auto Neighbors = PointToNeighbors[i];
		for (auto Neighbor : Neighbors)
		{
			Distances[TVector<uint32, 2>(i, Neighbor)] = ComputeDistance(InParticles, Neighbor, i);
		}
	}
	// Start and End Points to path and geodesic distance
	TMap<TVector<uint32, 2>, Pair<T, TArray<uint32>>> GeodesicPaths;
	// Dijkstra for each Kinematic Particle (assume a small number of kinematic points) - note this is N^2 log N with N kinematic points
	for (auto Element : KinematicParticles)
	{
		GeodesicPaths[TVector<uint32, 2>(Element, Element)] = {0, {Element}};
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			if (i != Element)
			{
				GeodesicPaths[TVector<int32, 2>(Element, i)] = {FLT_MAX, {}};
			}
		}
	}
	PhysicsParallelFor(KinematicParticles.Num(), [&](int32 Index) {
		auto Element = KinematicParticles[Index];
		std::priority_queue<Pair<T, uint32>, std::vector<Pair<T, uint32>>, std::greater<Pair<T, uint32>>> q;
		for (uint32 i = 0; i < InParticles.Size(); ++i)
		{
			q.push(MakePair(GeodesicPaths[TVector<uint32, 2>(Element, i)].First, i));
		}
		TSet<uint32> Visited;
		while (!q.empty())
		{
			auto PairElem = q.top();
			q.pop();
			if (Visited.Contains(PairElem.Second))
				continue;
			Visited.Add(PairElem.Second);
			auto CurrentStartEnd = TVector<uint32, 2>(Element, PairElem.Second);
			auto Neighbors = PointToNeighbors[PairElem.Second];
			for (auto Neighbor : Neighbors)
			{
				check(Neighbor != PairElem.Second);
				auto NeighborStartEnd = TVector<uint32, 2>(Element, Neighbor);
				auto NeighborDistancePath = GeodesicPaths[NeighborStartEnd];
				// Compute a possible distance for NeighborStartEnd
				T NewDist = PairElem.First + Distances[TVector<uint32, 2>(PairElem.Second, Neighbor)];
				if (NewDist < NeighborDistancePath.First)
				{
					auto NewPath = GeodesicPaths[CurrentStartEnd].Second;
					check(NewPath.Num() > 0 && NewPath[NewPath.Num() - 1] != Neighbor)
					    NewPath.Add(Neighbor);
					GeodesicPaths[NeighborStartEnd] = {NewDist, NewPath};
					q.push(MakePair(GeodesicPaths[NeighborStartEnd].First, Neighbor));
				}
			}
		}
	});
	FCriticalSection CriticalSection;
	PhysicsParallelFor(InParticles.Size(), [&](int32 i) {
		if (InParticles.InvM(i) == 0)
			return;
		TArray<Pair<T, int32>> ClosestElements;
		for (auto Elements : IslandElements)
		{
			int32 ClosestElement = -1;
			for (auto Element : Elements)
			{
				if (ClosestElement < 0 || GeodesicPaths[TVector<uint32, 2>(ClosestElement, i)].First > GeodesicPaths[TVector<uint32, 2>(Element, i)].First)
				{
					ClosestElement = Element;
				}
			}
			// Empty Island
			if (ClosestElement < 0)
				continue;
			TVector<uint32, 2> Index(ClosestElement, i);
			check(GeodesicPaths[Index].First != FLT_MAX);
			check(GeodesicPaths[Index].Second.Num() > 1);
			ClosestElements.Add(MakePair(GeodesicPaths[Index].First, ClosestElement));
		}
		// How to sort based on smalled first value of pair....
		ClosestElements.Sort();
		if (NumberOfAttachments < ClosestElements.Num())
		{
			ClosestElements.SetNum(NumberOfAttachments);
		}
		for (auto Element : ClosestElements)
		{
			TVector<uint32, 2> Index(Element.Second, i);
			check(GeodesicPaths[Index].First == Element.First);
			check(FGenericPlatformMath::Abs(Element.First - ComputeGeodesicDistance(InParticles, GeodesicPaths[Index].Second)) < 1e-4);
			CriticalSection.Lock();
			MConstraints.Add(GeodesicPaths[Index].Second);
			MDists.Add(Element.First);
			CriticalSection.Unlock();
		}
	});
	// TODO(mlentine): This should work by just reverse sorting and not needing the filtering but it may not be guaranteed. Work out if this is actually guaranteed or not.
	MConstraints.Sort([](const TArray<uint32>& Elem1, const TArray<uint32>& Elem2) { return Elem1.Num() > Elem2.Num(); });
	TArray<TArray<uint32>> NewConstraints;
	TArray<T> NewDists;
	TMap<uint32, TArray<uint32>> ProcessedPairs;
	for (uint32 i = 1; i < static_cast<uint32>(MConstraints.Num()); ++i)
	{
		if (ProcessedPairs.Contains(MConstraints[i].Last()))
		{
			check(MConstraints[i].Num() == ProcessedPairs[MConstraints[i].Last()].Num());
			for (uint32 j = 0; j < static_cast<uint32>(ProcessedPairs[MConstraints[i].Last()].Num()); ++j)
			{
				check(ProcessedPairs[MConstraints[i].Last()][j] == MConstraints[i][j]);
			}
			continue;
		}
		TArray<uint32> Path;
		T Dist = 0;
		Path.Add(MConstraints[i][0]);
		for (uint32 j = 1; j < static_cast<uint32>(MConstraints[i].Num() - 1); ++j)
		{
			Dist += (InParticles.X(MConstraints[i][j]) - InParticles.X(MConstraints[i][j - 1])).Size();
			Path.Add(MConstraints[i][j]);
			NewConstraints.Add(Path);
			NewDists.Add(Dist);
			ProcessedPairs.Add(MConstraints[i][j], Path);
		}
	}
	MDists = NewDists;
	MConstraints = NewConstraints;
}

template<class T, int d>
TVector<T, d> TPBDLongRangeConstraintsBase<T, d>::GetDelta(const TPBDParticles<T, d>& InParticles, const int32 i) const
{
	const TArray<uint32>& Constraint = MConstraints[i];
	check(Constraint.Num() > 1);
	const uint32 i1 = Constraint[0];
	const uint32 i2 = Constraint[Constraint.Num() - 1];
	const uint32 i2m1 = Constraint[Constraint.Num() - 2];
	check(InParticles.InvM(i1) == 0);
	check(InParticles.InvM(i2) > 0);
	const T Distance = ComputeGeodesicDistance(InParticles, Constraint);
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
}

template class Chaos::TPBDLongRangeConstraintsBase<float, 3>;
