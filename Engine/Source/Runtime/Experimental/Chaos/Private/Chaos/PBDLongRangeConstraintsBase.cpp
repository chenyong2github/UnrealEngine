// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDLongRangeConstraintsBase.h"

#include "Chaos/Map.h"
#include "Chaos/Vector.h"
#include "Chaos/Framework/Parallel.h"
#include "ChaosStats.h"

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Compute Geodesic Constraints"), STAT_ChaosClothComputeGeodesicConstraints, STATGROUP_Chaos);

using namespace Chaos;

FPBDLongRangeConstraintsBase::FPBDLongRangeConstraintsBase(
	const FPBDParticles& Particles,
	const int32 InParticleOffset,
	const int32 InParticleCount,
	const TMap<int32, TSet<int32>>& PointToNeighbors,
	const TConstArrayView<FReal>& StiffnessMultipliers,
	const int32 MaxNumTetherIslands,
	const FVec2& InStiffness,
	const FReal LimitScale,
	const EMode InMode)
	: TethersView(Tethers)
	, Stiffness(StiffnessMultipliers, InStiffness, InParticleCount)
	, Mode(InMode)
	, ParticleOffset(InParticleOffset)
	, ParticleCount(InParticleCount)
{
	switch (Mode)
	{
	case EMode::Euclidean:
		ComputeEuclideanConstraints(Particles, PointToNeighbors, MaxNumTetherIslands);
		break;
	case EMode::Geodesic:
		ComputeGeodesicConstraints(Particles, PointToNeighbors, MaxNumTetherIslands);
		break;
	default:
		unimplemented();
		break;
	}

	// Scale the tether's reference lengths
	for (FTether& Tether : Tethers)
	{
		Tether.RefLength *= LimitScale;
	}
}

TArray<TArray<int32>> FPBDLongRangeConstraintsBase::ComputeIslands(
    const TMap<int32, TSet<int32>>& PointToNeighbors,
    const TArray<int32>& KinematicParticles)
{
	// Compute Islands
	int32 NextIsland = 0;
	TArray<int32> FreeIslands;
	TArray<TArray<int32>> IslandElements;

	TMap<int32, int32> ParticleToIslandMap;
	ParticleToIslandMap.Reserve(KinematicParticles.Num());

	for (const int32 Element : KinematicParticles)
	{
		// Assign Element an island, possibly unionizing existing islands
		int32 Island = TNumericLimits<int32>::Max();

		// KinematicParticles is generated from the keys of PointToNeighbors, so
		// we don't need to check if Element exists.
		const TSet<int32>& Neighbors = PointToNeighbors[Element];
		for (const int32 Neighbor : Neighbors)
		{
			if (ParticleToIslandMap.Contains(Neighbor))
			{
				if (Island == TNumericLimits<int32>::Max())
				{
					// We don't have an assigned island yet.  Join with the island
					// of the neighbor.
					Island = ParticleToIslandMap[Neighbor];
				}
				else
				{
					const int32 OtherIsland = ParticleToIslandMap[Neighbor];
					if (OtherIsland != Island)
					{
						// This kinematic particle is connected to multiple islands.
						// Union them.
						for (const int32 OtherElement : IslandElements[OtherIsland])
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
		if (Island == TNumericLimits<int32>::Max())
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
	for (int32 IslandIndex = 0; IslandIndex < IslandElements.Num(); )
	{
		if (!IslandElements[IslandIndex].Num())
		{
			IslandElements.RemoveAtSwap(IslandIndex, 1, false);  // RemoveAtSwap takes the last elements to replace the current one, do not increment the index in this case
		}
		else
		{
			++IslandIndex;
		}
	}

	return IslandElements;
}

void FPBDLongRangeConstraintsBase::ComputeEuclideanConstraints(
    const FPBDParticles& Particles,
    const TMap<int32, TSet<int32>>& PointToNeighbors,
    const int32 MaxNumTetherIslands)
{
	// Fill up the list of all used indices
	TArray<int32> Nodes;
	PointToNeighbors.GenerateKeyArray(Nodes);

	TArray<int32> KinematicParticles;
	for (const int32 Node : Nodes)
	{
		if (Particles.InvM(Node) == (FReal)0.)
		{
			KinematicParticles.Add(Node);
		}
	}

	// Compute the islands of kinematic particles
	const TArray<TArray<int32>> IslandElements = ComputeIslands(PointToNeighbors, KinematicParticles);
	int32 NumTotalIslandElements = 0;
	for (const TArray<int32>& Elements : IslandElements)
	{
		NumTotalIslandElements += Elements.Num();
	}

	TArray<TArray<FTether>> NewTethersSlots;
	NewTethersSlots.SetNum(Nodes.Num());

	for (TArray<FTether>& NewTethers : NewTethersSlots)
	{
		NewTethers.Reserve(MaxNumTetherIslands);
	}

	int32 NumTethers = 0;

	PhysicsParallelFor(Nodes.Num(), [&](int32 Index)
	{
		const int32 Node = Nodes[Index];

		// For each non-kinematic particle i...
		if (Particles.InvM(Node) == (FReal)0.)
		{
			return;
		}

		// Measure the distance to all kinematic particles in all islands...
		TArray<Pair<FReal, int32>> ClosestElements;
		ClosestElements.Reserve(NumTotalIslandElements);

		for (const TArray<int32>& Elements : IslandElements)
		{
			// IslandElements may contain empty arrays.
			if (Elements.Num() == 0)
			{
				continue;
			}

			int32 ClosestElement = TNumericLimits<int32>::Max();
			FReal ClosestSquareDistance = TNumericLimits<FReal>::Max();
			for (const int32 Element : Elements)
			{
				const FReal SquareDistance = (Particles.X(Element) - Particles.X(Node)).SizeSquared();
				if (SquareDistance < ClosestSquareDistance)
				{
					ClosestElement = Element;
					ClosestSquareDistance = SquareDistance;
				}
			}
			ClosestElements.Add(MakePair(FMath::Sqrt(ClosestSquareDistance), ClosestElement));
		}

		// Order all by distance, smallest first...
		ClosestElements.Sort();

		// Take the first MaxNumTetherIslands.
		if (MaxNumTetherIslands < ClosestElements.Num())
		{
			ClosestElements.SetNum(MaxNumTetherIslands);
		}

		// Add constraints between this particle, and the N closest...
		for (const Pair<FReal, int32> Element : ClosestElements)
		{
			NewTethersSlots[Index].Emplace(Element.Second, Node, Element.First);
			++NumTethers;
		}
	});

	// Consolidate the N slots into one single list of tethers, spreading the nodes across different ranges for parallel processing
	Tethers.Reset(NumTethers);

	for (int32 Slot = 0; Slot < MaxNumTetherIslands; ++Slot)
	{
		int32 Size = 0;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (NewTethersSlots[Index].IsValidIndex(Slot))
			{
				Tethers.Emplace(NewTethersSlots[Index][Slot]);
				++Size;
			}
		}
		TethersView.AddRange(Size);
	}
}

void FPBDLongRangeConstraintsBase::ComputeGeodesicConstraints(
    const FPBDParticles& Particles,
    const TMap<int32, TSet<int32>>& PointToNeighbors,
    const int32 MaxNumTetherIslands)
{
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothComputeGeodesicConstraints);

	// Fill up the list of all used indices
	TArray<int32> Nodes;
	PointToNeighbors.GenerateKeyArray(Nodes);

	// Find all kinematic points to use as anchor points for the tethers and seed the path finding
	TArray<int32> Seeds;  // Kinematic nodes connected to at least one dynamic node
	Seeds.Reserve(Nodes.Num() / 10);  // Start at 10% seeds to minimize this array's reallocation

	int32 NumDynamicNodes = 0;

	for (const int32 Node : Nodes)
	{
		if (Particles.InvM(Node) == (FReal)0.)
		{
			const TSet<int32>& Neighbors = PointToNeighbors[Node];
			for (const int32 Neighbor : Neighbors)
			{
				if (Particles.InvM(Neighbor) != (FReal)0.)  // There's no point to keep a node as an anchor, unless at least one of its neighbors is dynamic
				{
					Seeds.Add(Node);
					break;
				}
			}
		}
		else
		{
			++NumDynamicNodes;
		}
	}

	// Dijkstra for each Kinematic Particle (assume a small number of kinematic points) - note this is N^2 log N with N kinematic points
	// Find shortest paths from each kinematic node to all dynamic nodes
	TMap<TVec2<int32>, FReal> GeodesicDistances;
	GeodesicDistances.Reserve(Seeds.Num() * NumDynamicNodes);

	for (const int32 Seed : Seeds)
	{
		for (const int32 Node : Nodes)
		{
			if (Particles.InvM(Node) != (FReal)0.)
			{
				GeodesicDistances.Emplace(TVec2<int32>(Seed, Node), Node != Seed ? TNumericLimits<FReal>::Max() : (FReal)0.);
			}
		}
	}

	PhysicsParallelFor(Seeds.Num(), [&](int32 Index)
	{
		const int32 Seed = Seeds[Index];

		// Keep track of all visited nodes in a bit array, will need to be offsetted since the first node index may not be 0
		TBitArray<> VisitedNodes(false, Nodes.Num());

		// Priority queue based implementation of the Dijkstra algorithm
#define CHAOS_PBDLONGRANGEATTACHMENT_PROFILE_HEAPSIZE 0
#if CHAOS_PBDLONGRANGEATTACHMENT_PROFILE_HEAPSIZE
		static int32 MaxHeapSize = 1;  // Record the max heap size
#else
		static const int32 MaxHeapSize = 512;  // Set the queue size to something large enough to avoid reallocations in most cases
#endif
		auto LessPredicate = [Seed, &GeodesicDistances](int32 Node1, int32 Node2) -> bool
			{
				return GeodesicDistances[TVec2<int32>(Seed, Node1)] < GeodesicDistances[TVec2<int32>(Seed, Node2)];  // Less for node priority
			};

		TArray<int32> Queue;
		Queue.Reserve(MaxHeapSize);
		Queue.Heapify(LessPredicate);  // Turn the array into a priority queue

		// Initiate the graph progression
		VisitedNodes[Seed - ParticleOffset] = true;
		Queue.HeapPush(Seed, LessPredicate);

		do
		{
			int32 ParentNode;
			Queue.HeapPop(ParentNode, LessPredicate, false);

			check(VisitedNodes[ParentNode - ParticleOffset]);

			const FReal ParentDistance = (ParentNode != Seed) ? GeodesicDistances[TVec2<int32>(Seed, ParentNode)] : (FReal)0.;

			const TSet<int32>& NeighborNodes = PointToNeighbors[ParentNode];
			for (const int32 NeighborNode : NeighborNodes)
			{
				check(NeighborNode != ParentNode);

				// Do not progress onto kinematic nodes
				if (Particles.InvM(NeighborNode) == (FReal)0.)
				{
					continue;
				}

				// Update the geodesic distance if this path is a shorter one
				const FReal NewDistance = ParentDistance + (Particles.X(NeighborNode) - Particles.X(ParentNode)).Size();

				FReal& GeodesicDistance = GeodesicDistances[TVec2<int32>(Seed, NeighborNode)];

				if (NewDistance < GeodesicDistance)
				{
					// Update this path distance
					GeodesicDistance = NewDistance;

					// Progress to this node position if it hasn't yet been visited
					if (!VisitedNodes[NeighborNode - ParticleOffset])
					{
						VisitedNodes[NeighborNode - ParticleOffset] = true;

						Queue.HeapPush(NeighborNode, LessPredicate);

#if CHAOS_PBDLONGRANGEATTACHMENT_PROFILE_HEAPSIZE
						MaxHeapSize = FMath::Max(Queue.Num(), MaxHeapSize);
#endif
					}
				}
			}
		} while (Queue.Num());
	});

	// Compute islands of kinematic points
	const TArray<TArray<int32>> Islands = ComputeIslands(PointToNeighbors, Seeds);

	// Find the tether constraints starting from each dynamic node
	TArray<TArray<FTether>> NewTethersSlots;
	NewTethersSlots.SetNum(Nodes.Num());

	for (TArray<FTether>& NewTethers : NewTethersSlots)
	{
		NewTethers.Reserve(MaxNumTetherIslands);
	}

	TAtomic<int32> NumTethers(0);

	PhysicsParallelFor(Nodes.Num(), [&](int32 Index)
	{
		const int32 Node = Nodes[Index];
		if (Particles.InvM(Node) == (FReal)0.)
		{
			return;  // No tethers on dynamic particles
		}

		// Find the closest seeds in each island
		TArray<int32, TInlineAllocator<32>> ClosestSeeds;
		ClosestSeeds.Reserve(Islands.Num());

		for (const TArray<int32>& Seeds : Islands)
		{
			if (!Seeds.Num())
			{
				continue;  // Empty island 
			}

			int32 ClosestSeed = INDEX_NONE;
			FReal ClosestDistance = TNumericLimits<FReal>::Max();

			for (const int32 Seed : Seeds)
			{
				const FReal Distance = GeodesicDistances[TVec2<int32>(Seed, Node)];
				if (Distance < ClosestDistance)
				{
					ClosestDistance = Distance;
					ClosestSeed = Seed;
				}
			}
			if (ClosestSeed != INDEX_NONE)
			{
				ClosestSeeds.Add(ClosestSeed);
			}
		}

		// Sort all the tethers for this node based on smallest distance
		ClosestSeeds.Sort([&GeodesicDistances, Node](int32 Seed1, int32 Seed2)
			{
				return GeodesicDistances[TVec2<int32>(Seed1, Node)] < GeodesicDistances[TVec2<int32>(Seed2, Node)];
			});

		// Keep only N closest tethers
		if (MaxNumTetherIslands < ClosestSeeds.Num())
		{
			ClosestSeeds.SetNum(MaxNumTetherIslands);
		}

		// Add these tethers to the N (or less) available slots
		for (const int32 Seed : ClosestSeeds)
		{
			const FReal RefLength = GeodesicDistances[TVec2<int32>(Seed, Node)];
			NewTethersSlots[Index].Emplace(Seed, Node, RefLength);
			++NumTethers;
		}
		check(NewTethersSlots[Index].Num() <= MaxNumTetherIslands);
	});

	// Consolidate the N slots into one single list of tethers, spreading the nodes across different ranges for parallel processing
	Tethers.Reset(NumTethers);

	for (int32 Slot = 0; Slot < MaxNumTetherIslands; ++Slot)
	{
		int32 Size = 0;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (NewTethersSlots[Index].IsValidIndex(Slot))
			{
				Tethers.Emplace(NewTethersSlots[Index][Slot]);
				++Size;
			}
		}
		TethersView.AddRange(Size);
	}
}
