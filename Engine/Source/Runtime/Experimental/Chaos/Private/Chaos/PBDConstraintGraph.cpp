// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintGraph.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "ChaosStats.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/PBDRigidParticles.h"
#include "Containers/Queue.h"

using namespace Chaos;

template<typename T, int d>
TPBDConstraintGraph<T, d>::TPBDConstraintGraph()
{
}

template<typename T, int d>
TPBDConstraintGraph<T, d>::TPBDConstraintGraph(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices)
{
	InitializeGraph(InParticles, InIndices);
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::InitializeGraph(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices)
{
	const int32 NumNonDisabledParticles = InIndices.Num();

	Nodes.Reset();
	Nodes.AddDefaulted(NumNonDisabledParticles);

	Edges.Reset();

	ParticleToNodeIndex.Reset();
	ParticleToNodeIndex.Reserve(NumNonDisabledParticles);
	for(int32 Index = 0; Index < NumNonDisabledParticles; ++Index)
	{
		FGraphNode& Node = Nodes[Index];
		Node.BodyIndex = InIndices[Index];
		ParticleToNodeIndex.Add(Node.BodyIndex, Index);
	}

	//@todo(ocohen): Should we reset more than just the edges? What about bIsIslandPersistant?
	for (TArray<int32>& IslandConstraintList : IslandConstraints)
	{
		IslandConstraintList.Reset();
	}
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::ResetIslands(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices)
{
	//@todo(ocohen): Should we reset more than just the edges? What about bIsIslandPersistant?
	for (TArray<int32>& IslandConstraintList : IslandConstraints)
	{
		IslandConstraintList.Reset();
	}

	const int32 NumBodies = InIndices.Num();
	for(int32 BodyIdx = 0; BodyIdx < NumBodies; ++BodyIdx)	//@todo(ocohen): could go wide per island if we can get at the sets
	{
		const int32 ParticleIndex = InIndices[BodyIdx];
		const int32 Island = InParticles.Island(ParticleIndex);
		if (Island >= 0)
		{
			FGraphNode& Node = Nodes[BodyIdx];
			Node.Island = Island;
			for (int32 ConstraintDataIndex : Node.Edges)
			{
				IslandConstraints[Island].Add(ConstraintDataIndex);
			}
		}
	}
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::ReserveConstraints(const int32 NumConstraints)
{
	Edges.Reserve(Edges.Num() + NumConstraints);
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::AddConstraint(const uint32 InContainerId, const int32 InConstraintIndex, const TVector<int32, 2>& InConstrainedParticles)
{
	// Must have at least one constrained particle
	check((InConstrainedParticles[0] != INDEX_NONE) || (InConstrainedParticles[1] != INDEX_NONE));

	const int32 NewEdgeIndex = Edges.Num();
	FGraphEdge NewEdge;
	NewEdge.Data = { InContainerId, InConstraintIndex };

	if (InConstrainedParticles[0] != INDEX_NONE)
	{
		NewEdge.FirstNode = ParticleToNodeIndex[InConstrainedParticles[0]];
		Nodes[NewEdge.FirstNode].BodyIndex = InConstrainedParticles[0];
		Nodes[NewEdge.FirstNode].Edges.Add(NewEdgeIndex);
	}
	if (InConstrainedParticles[1] != INDEX_NONE)
	{
		NewEdge.SecondNode = ParticleToNodeIndex[InConstrainedParticles[1]];
		Nodes[NewEdge.SecondNode].BodyIndex = InConstrainedParticles[1];
		Nodes[NewEdge.SecondNode].Edges.Add(NewEdgeIndex);
	}

	Edges.Add(MoveTemp(NewEdge));
}

template<typename T, int d>
const typename TPBDConstraintGraph<T, d>::FConstraintData& TPBDConstraintGraph<T, d>::GetConstraintData(int32 ConstraintDataIndex) const
{
	return Edges[ConstraintDataIndex].Data;
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::UpdateIslands(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, TSet<int32>& ActiveIndices)
{
	// Maybe expose a memset style function for this instead of iterating
	for (uint32 i = 0; i < InParticles.Size(); ++i)
	{
		InParticles.Island(i) = INDEX_NONE;
	}
	ComputeIslands(InParticles, InIndices, ActiveIndices);
}

DECLARE_CYCLE_STAT(TEXT("IslandGeneration"), STAT_IslandGeneration, STATGROUP_Chaos);

template<typename T, int d>
void TPBDConstraintGraph<T, d>::ComputeIslands(TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, TSet<int32>& ActiveIndices)
{
	SCOPE_CYCLE_COUNTER(STAT_IslandGeneration);

	int32 NextIsland = 0;
	TArray<TSet<int32>> NewIslandParticles;
	TArray<int32> NewIslandSleepCounts;

	const int32 NumNodes = InIndices.Num();
	for(int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
	{
		const int32 ParticleIndex = InIndices[NodeIndex];

		if (Nodes[NodeIndex].Island >= 0 || InParticles.InvM(ParticleIndex) == 0)
		{
			// Island is already known - it was visited in ComputeIsland for a previous node
			continue;
		}

		TSet<int32> SingleIslandParticles;
		TSet<int32> SingleIslandStaticParticles;
		ComputeIsland(InParticles, InIndices, NodeIndex, NextIsland, SingleIslandParticles, SingleIslandStaticParticles);
		
		for (const int32& StaticParticle : SingleIslandStaticParticles)
		{
			SingleIslandParticles.Add(StaticParticle);
		}

		if (SingleIslandParticles.Num())
		{
			NewIslandParticles.SetNum(NextIsland + 1);
			NewIslandParticles[NextIsland] = MoveTemp(SingleIslandParticles);
			NextIsland++;
		}
	}

	IslandConstraints.SetNum(NextIsland);
	IslandData.SetNum(NextIsland);

	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		const FGraphEdge& Edge = Edges[EdgeIndex];
		int32 FirstIsland = (Edge.FirstNode != INDEX_NONE)? Nodes[Edge.FirstNode].Island : INDEX_NONE;
		int32 SecondIsland = (Edge.SecondNode != INDEX_NONE)? Nodes[Edge.SecondNode].Island : INDEX_NONE;
		check(FirstIsland == SecondIsland || FirstIsland == INDEX_NONE || SecondIsland == INDEX_NONE);

		int32 Island = (FirstIsland != INDEX_NONE)? FirstIsland : SecondIsland;
		check(Island >= 0);

		IslandConstraints[Island].Add(EdgeIndex);
	}

	NewIslandSleepCounts.SetNum(NewIslandParticles.Num());

	if (NewIslandParticles.Num())
	{
		for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
		{
			NewIslandSleepCounts[Island] = 0;

			for (const int32& Index : NewIslandParticles[Island])
			{
				if (InParticles.InvM(Index))
				{
					InParticles.Island(Index) = Island;
				}
				else
				{
					InParticles.Island(Index) = INDEX_NONE;
				}
			}
		}
		// Force consistent state if no previous islands
		if (!IslandParticles.Num())
		{
			for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
			{
				IslandData[Island].bIsIslandPersistant = true;
				bool bSleepState = true;

				for (const int32& Index : NewIslandParticles[Island])
				{
					if (!InParticles.Sleeping(Index))
					{
						bSleepState = false;
						break;
					}
				}

				for (const int32& Index : NewIslandParticles[Island])
				{
					//@todo(DEMO_HACK) : Need to fix, remove the !InParticles.Disabled(Index)
					if (InParticles.Sleeping(Index) && !bSleepState && !InParticles.Disabled(Index))
					{
						ActiveIndices.Add(Index);
					}

					if (!InParticles.Sleeping(Index) && bSleepState && InParticles.InvM(Index))
					{
						ActiveIndices.Remove(Index);
						InParticles.V(Index) = TVector<T, d>(0);
						InParticles.W(Index) = TVector<T, d>(0);
					}

					if (InParticles.InvM(Index))
					{
						InParticles.SetSleeping(Index, bSleepState);
					}

					if ((InParticles.Sleeping(Index) || InParticles.Disabled(Index)) && ActiveIndices.Contains(Index))
					{
						ActiveIndices.Remove(Index);
					}
				}
			}
		}

		for (int32 Island = 0; Island < IslandParticles.Num(); ++Island)
		{
			bool bIsSameIsland = true;

			// Objects were removed from the island
			int32 OtherIsland = -1;

			for (const int32 Index : IslandParticles[Island])
			{
				int32 TmpIsland = InParticles.Island(Index);

				if (OtherIsland == INDEX_NONE && TmpIsland >= 0)
				{
					OtherIsland = TmpIsland;
				}
				else
				{
					if (TmpIsland >= 0 && OtherIsland != TmpIsland)
					{
						bIsSameIsland = false;
						break;
					}
				}
			}

			// A new object entered the island or the island is entirely new particles
			if (bIsSameIsland && (OtherIsland == INDEX_NONE || NewIslandParticles[OtherIsland].Num() != IslandParticles[Island].Num()))
			{
				bIsSameIsland = false;
			}

			// Find out if we need to activate island
			if (bIsSameIsland)
			{
				NewIslandSleepCounts[OtherIsland] = IslandSleepCounts[Island];
			}
			else
			{
				for (const int32 Index : IslandParticles[Island])
				{
					if (!InParticles.Disabled(Index))
					{
						InParticles.SetSleeping(Index, false);
						ActiveIndices.Add(Index);
					}
				}
			}

			// #BG Necessary? Should we ever not find an island?
			if (OtherIsland != INDEX_NONE)
			{
				IslandData[OtherIsland].bIsIslandPersistant = bIsSameIsland;
			}
		}

		//if any particles are awake, make sure the entire island is awake
#if 0
		for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
		{
			int32 OtherIsland = -1;
			for (const int32 Index : NewIslandParticles[Island])
			{
				if (!InParticles.Sleeping(Index) && !InParticles.InvM(Index))
				{
					for (const int32 Index : NewIslandParticles[Island])
					{
						if (!InParticles.InvM(Index))
						{
							InParticles.Sleeping(Index) = false;
							ActiveIndices.Add(Index);
						}
					}
					IslandData[Island].bIsIslandPersistant = false;
					break;
				}
			}
		}
#endif
	}

	IslandParticles.Reset();
	IslandParticles.Reserve(NewIslandParticles.Num());
	for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
	{
		IslandParticles.Emplace(NewIslandParticles[Island].Array());
	}
	IslandSleepCounts = MoveTemp(NewIslandSleepCounts);
	
	check(IslandParticles.Num() == IslandSleepCounts.Num());
	check(IslandParticles.Num() == IslandConstraints.Num());
	check(IslandParticles.Num() == IslandData.Num());
	// @todo(ccaulfield): make a more complex unit test to check island integrity
	//checkSlow(CheckIslands(InParticles, ActiveIndices));
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::ComputeIsland(const TPBDRigidParticles<T, d>& InParticles, const TArray<int32>& InIndices, int32 InNode, const int32 Island,
    TSet<int32>& DynamicParticlesInIsland, TSet<int32>& StaticParticlesInIsland)
{
	TQueue<int32> NodeQueue;
	NodeQueue.Enqueue(InNode);
	while (!NodeQueue.IsEmpty())
	{
		int32 NodeIndex;
		NodeQueue.Dequeue(NodeIndex);
		FGraphNode& Node = Nodes[NodeIndex];

		if (Node.Island >= 0)
		{
			check(Node.Island == Island);
			continue;
		}

		if (!InParticles.InvM(Node.BodyIndex))
		{
			if (!StaticParticlesInIsland.Contains(Node.BodyIndex))
			{
				StaticParticlesInIsland.Add(Node.BodyIndex);
			}
			continue;
		}

		DynamicParticlesInIsland.Add(Node.BodyIndex);
		Node.Island = Island;

		for (const int32 EdgeIndex : Node.Edges)
		{
			const FGraphEdge& Edge = Edges[EdgeIndex];
			int32 OtherNode = INDEX_NONE;
			if (NodeIndex == Edge.FirstNode)
			{
				OtherNode = Edge.SecondNode;
			}
			if (NodeIndex == Edge.SecondNode)
			{
				OtherNode = Edge.FirstNode;
			}
			if (OtherNode != INDEX_NONE)
			{
				NodeQueue.Enqueue(OtherNode);
			}
		}
	}
}

template<typename T, int d>
bool TPBDConstraintGraph<T, d>::SleepInactive(TPBDRigidParticles<T, d>& InParticles, const int32 Island, const TArrayCollectionArray<TSerializablePtr<TChaosPhysicsMaterial<T>>>& PerParticleMaterialAttributes)
{
	// @todo(ccaulfield): should be able to eliminate this when island is already sleeping

	const TArray<int32>& IslandParticleIndices = GetIslandParticles(Island);
	check(IslandParticleIndices.Num());

	if (!IslandData[Island].bIsIslandPersistant)
	{
		return false;
	}

	int32& IslandSleepCount = IslandSleepCounts[Island];

	TVector<T, d> X(0);
	TVector<T, d> V(0);
	TVector<T, d> W(0);
	T M = 0;
	T LinearSleepingThreshold = FLT_MAX;
	T AngularSleepingThreshold = FLT_MAX;

	for (const int32 Index : IslandParticleIndices)
	{
		if (!InParticles.InvM(Index))
		{
			continue;
		}
		X += InParticles.X(Index) * InParticles.M(Index);
		M += InParticles.M(Index);
		V += InParticles.V(Index) * InParticles.M(Index);

		if (PerParticleMaterialAttributes[Index])
		{
			LinearSleepingThreshold = FMath::Min(LinearSleepingThreshold, PerParticleMaterialAttributes[Index]->SleepingLinearThreshold);
			AngularSleepingThreshold = FMath::Min(AngularSleepingThreshold, PerParticleMaterialAttributes[Index]->SleepingAngularThreshold);
		}
		else
		{
			LinearSleepingThreshold = (T)0;
			LinearSleepingThreshold = (T)0;
		}
	}

	X /= M;
	V /= M;

	for (const int32 Index : IslandParticleIndices)
	{
		if (!InParticles.InvM(Index))
		{
			continue;
		}
		W += /*TVector<T, d>::CrossProduct(InParticles.X(Index) - X, InParticles.M(Index) * InParticles.V(Index)/ +*/ InParticles.W(Index) * InParticles.M(Index);
	}

	W /= M;

	const T VSize = V.SizeSquared();
	const T WSize = W.SizeSquared();

	if (VSize < LinearSleepingThreshold && WSize < AngularSleepingThreshold)
	{
		if (IslandSleepCount > SleepCountThreshold)
		{
			for (const int32 Index : IslandParticleIndices)
			{
				if (!InParticles.InvM(Index))
				{
					continue;
				}
				InParticles.SetSleeping(Index, true);
				InParticles.V(Index) = TVector<T, d>(0);
				InParticles.W(Index) = TVector<T, d>(0);
			}
			return true;
		}
		else
		{
			IslandSleepCount++;
		}
	}

	return false;
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::WakeIsland(TPBDRigidParticles<T, d>& InParticles, const int32 Island)
{
	for (int32 Particle : IslandParticles[Island])
	{
		if (InParticles.Sleeping(Particle))
		{
			InParticles.SetSleeping(Particle, false);
		}
	}
	IslandSleepCounts[Island] = 0;
}


template<typename T, int d>
void TPBDConstraintGraph<T, d>::ReconcileIslands(TPBDRigidParticles<T, d>& InParticles)
{
	for (int32 i = 0; i < IslandParticles.Num(); ++i)
	{
		bool IsSleeping = true;
		bool IsSet = false;
		for (const int32 Index : IslandParticles[i])
		{
			if (InParticles.ObjectState(Index) == EObjectStateType::Static)
			{
				continue;
			}
			if (!IsSet)
			{
				IsSet = true;
				IsSleeping = InParticles.Sleeping(Index);
			}
			if (InParticles.Sleeping(Index) != IsSleeping)
			{
				WakeIsland(InParticles, i);
				break;
			}
		}
	}
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::EnableParticle(TPBDRigidParticles<T, d>& InParticles, const int32 ParticleIndex, const int32 ParentParticleIndex)
{
	if (ParentParticleIndex != INDEX_NONE)
	{
		const int32 Island = InParticles.Island(ParentParticleIndex);
		InParticles.Island(ParticleIndex) = Island;
		if (IslandParticles.IsValidIndex(Island))
		{
			IslandParticles[Island].Add(ParticleIndex);
		}

		const bool SleepState = InParticles.Sleeping(ParentParticleIndex);
		InParticles.SetSleeping(ParticleIndex, SleepState);
	}
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::DisableParticle(TPBDRigidParticles<T, d>& InParticles, const int32 ParticleIndex)
{
	const int32 Island = InParticles.Island(ParticleIndex);
	if (Island != INDEX_NONE)
	{
		InParticles.Island(ParticleIndex) = INDEX_NONE;

		// @todo(ccaulfield): optimize
		if (IslandParticles.IsValidIndex(Island))
		{
			int32 IslandParticleIndex = IslandParticles[Island].Find(ParticleIndex);
			check(IslandParticleIndex != INDEX_NONE);
			IslandParticles[Island].RemoveAtSwap(IslandParticleIndex);
		}
	}
}

template<typename T, int d>
void TPBDConstraintGraph<T, d>::DisableParticles(TPBDRigidParticles<T, d>& InParticles, const TSet<int32>& InParticleIndices)
{
	// @todo(ccaulfield): optimize
	for (int32 ParticleIndex : InParticleIndices)
	{
		DisableParticle(InParticles, ParticleIndex);
	}
}

template<typename T, int d>
bool TPBDConstraintGraph<T, d>::CheckIslands(TPBDRigidParticles<T, d>& InParticles, const TSet<int32>& InParticleIndices)
{
	bool bIsValid = true;

	// Check that no particles are in multiple islands
	TSet<int32> IslandParticlesUnionSet;
	IslandParticlesUnionSet.Reserve(InParticles.Size());
	for (int32 Island = 0; Island < IslandParticles.Num(); ++Island)
	{
		TSet<int32> IslandParticlesSet = TSet<int32>(IslandParticles[Island]);
		TSet<int32> IslandParticlesIntersectSet = IslandParticlesUnionSet.Intersect(IslandParticlesSet);
		if (IslandParticlesIntersectSet.Num() > 0)
		{
			// This islands contains particles that were in a previous island.
			// This is ok only if those particles are static
			for (const int32 ParticleIndex : IslandParticlesIntersectSet)
			{
				if (!InParticles.HasInfiniteMass(ParticleIndex))
				{
					UE_LOG(LogChaos, Error, TEXT("Island %d contains non-static particle %d that is also in another Island"), Island, ParticleIndex);
					bIsValid = false;
				}
			}
		}
		IslandParticlesUnionSet = IslandParticlesUnionSet.Union(IslandParticlesSet);
	}

	// Check that no constraints refer in the same island
	TSet<int32> IslandConstraintDataUnionSet;
	IslandConstraintDataUnionSet.Reserve(Edges.Num());
	for (int32 Island = 0; Island < IslandConstraints.Num(); ++Island)
	{
		TSet<int32> IslandConstraintDataSet = TSet<int32>(IslandConstraints[Island]);
		TSet<int32> IslandConstraintDataIntersectSet = IslandConstraintDataUnionSet.Intersect(IslandConstraintDataSet);
		if (IslandConstraintDataIntersectSet.Num() > 0)
		{
			// This islands contains constraints that were in a previous island
			UE_LOG(LogChaos, Error, TEXT("Island %d contains Constraints in another Island"), Island);
			bIsValid = false;
		}
		IslandConstraintDataUnionSet = IslandConstraintDataUnionSet.Union(IslandConstraintDataSet);
	}

	return bIsValid;
}


template class Chaos::TPBDConstraintGraph<float, 3>;
