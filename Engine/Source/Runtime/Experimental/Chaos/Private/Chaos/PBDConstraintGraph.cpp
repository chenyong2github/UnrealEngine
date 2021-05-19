// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/PBDConstraintGraph.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "ChaosStats.h"
#include "ChaosLog.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/PBDRigidParticles.h"
#include "Containers/Queue.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosLog.h"

using namespace Chaos;

bool ChaosSolverSleepEnabled = true;
FAutoConsoleVariableRef CVarChaosSolverSleepEnabled(TEXT("p.Chaos.Solver.SleepEnabled"), ChaosSolverSleepEnabled, TEXT(""));

bool ChaosSolverCollisionDefaultUseMaterialSleepThresholdsCVar = true;
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultUseMaterialSleepThresholds(TEXT("p.ChaosSolverCollisionDefaultUseMaterialSleepThresholds"), ChaosSolverCollisionDefaultUseMaterialSleepThresholdsCVar, TEXT("Enable material support for sleeping thresholds[def:true]"));

int32 ChaosSolverCollisionDefaultSleepCounterThresholdCVar = 20; 
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultSleepCounterThreshold(TEXT("p.ChaosSolverCollisionDefaultSleepCounterThreshold"), ChaosSolverCollisionDefaultSleepCounterThresholdCVar, TEXT("Default counter threshold for sleeping.[def:20]"));

Chaos::FRealSingle ChaosSolverCollisionDefaultLinearSleepThresholdCVar = 0.001f; // .001 unit mass cm
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultLinearSleepThreshold(TEXT("p.ChaosSolverCollisionDefaultLinearSleepThreshold"), ChaosSolverCollisionDefaultLinearSleepThresholdCVar, TEXT("Default linear threshold for sleeping.[def:0.001]"));

Chaos::FRealSingle ChaosSolverCollisionDefaultAngularSleepThresholdCVar = 0.0087f;  //~1/2 unit mass degree
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultAngularSleepThreshold(TEXT("p.ChaosSolverCollisionDefaultAngularSleepThreshold"), ChaosSolverCollisionDefaultAngularSleepThresholdCVar, TEXT("Default angular threshold for sleeping.[def:0.0087]"));

FPBDConstraintGraph::FPBDConstraintGraph() : VisitToken(0)
{
}


FPBDConstraintGraph::FPBDConstraintGraph(const TParticleView<FGeometryParticles>& Particles) : VisitToken(0)
{
	InitializeGraph(Particles);
}

int32 FPBDConstraintGraph::ReserveParticles(const int32 Num)
{
	const int32 NumFree = FreeIndexList.Num();
	int32 NumToAdd = NumFree - Num; // Can be negative
	if (NumToAdd > 0)
	{
		return 0;
	}
	NumToAdd = -NumToAdd; // Flip sign
	const int32 NumNodes = Nodes.Num();
	Nodes.SetNum(NumNodes + NumToAdd);
	FreeIndexList.Reserve(NumToAdd);
	for (int32 Counter = 0; Counter < NumToAdd; Counter++)
	{
		FreeIndexList.Push(NumNodes + Counter);
	}

	ParticleToNodeIndex.Reserve(ParticleToNodeIndex.Num() + NumToAdd);
	Visited.Reserve(Visited.Num() + NumToAdd);

	return NumToAdd;
}

/**
 * Bill added this.
 * Adds new Node to Nodes array when a new particle is created
 */

void FPBDConstraintGraph::ParticleAdd(FGeometryParticleHandle* AddedParticle)
{
	// GC Code creates particle then enables particle so end up calling this twice triggering ensure
	if (/*ensure*/ (!ParticleToNodeIndex.Contains(AddedParticle)))
	{
		const int32 NewNodeIndex = GetNextNodeIndex();
		FGraphNode& Node = Nodes[NewNodeIndex];
		ensure(Node.Edges.Num() == 0);
		ensure(Node.Island == INDEX_NONE);

		Node.Particle = AddedParticle->Handle();
		check(Node.Particle);
		ParticleToNodeIndex.Add(Node.Particle, NewNodeIndex);

		const int32 NewMinNum = NewNodeIndex + 1;
		const int32 NumToAdd = NewMinNum - Visited.Num();
		if (NumToAdd > 0)
			Visited.AddZeroed(NumToAdd);
		else
			Visited[NewNodeIndex] = 0;
	}
}

/**
 * Bill added this
 * Removes Node from Nodes array - marking it an unused, also clears ParticleToNodeIndex
 */

void FPBDConstraintGraph::ParticleRemove(FGeometryParticleHandle* RemovedParticle)
{
	if (ParticleToNodeIndex.Contains(RemovedParticle))
	{
		const int32 NodeIdx = ParticleToNodeIndex[RemovedParticle];
		FreeIndexList.Push(NodeIdx);

		FGraphNode& NodeRemoved = Nodes[NodeIdx];
		NodeRemoved.Edges.Empty();
		NodeRemoved.Particle = nullptr;
		NodeRemoved.Island = INDEX_NONE;

		Visited[NodeIdx]=0;
		ParticleToNodeIndex.Remove(RemovedParticle);
		UpdatedNodes.RemoveSwap(NodeIdx, false);
	}
}



int32 FPBDConstraintGraph::GetNextNodeIndex()
{
	int32 NewNodeIndex = Nodes.Num();
	if (FreeIndexList.Num() > 0)
	{
		NewNodeIndex = FreeIndexList.Pop();
	}

	if (Nodes.Num() <= NewNodeIndex)
	{
		Nodes.SetNum(NewNodeIndex + 1);
	}

	return NewNodeIndex;
}

/**
 * Called every frame, it used to clear all Nodes, Edges, ParticleToNodeIndex
 * then AddToGraph on constraint rules would fill up
 *
 * Now clears Edges and attempts to retain Nodes and ParticleToNodeIndex
 * It is still setting up nodes that don't have a constraint so wasted effort iterating over all nodes, better to iterate over constraints or don't fill out Nodes if they don't have a constraint
 */

void FPBDConstraintGraph::InitializeGraph(const TParticleView<FGeometryParticles>& Particles)
{
	const int32 NumNonDisabledParticles = Particles.Num();

	if(NumNonDisabledParticles && Nodes.Num()==0)
	{
		ensure(FreeIndexList.Num()==0);
		Nodes.Reset();
		Nodes.AddDefaulted(NumNonDisabledParticles);

		ParticleToNodeIndex.Reset();
		ParticleToNodeIndex.Reserve(NumNonDisabledParticles);
		{
			int32 Index = 0;
			for (auto& Particle : Particles)
			{
				FGraphNode& Node = Nodes[Index];
				Node.Particle = Particle.Handle();
				ParticleToNodeIndex.Add(Node.Particle, Index);
				++Index;
			}
		}

		Visited.Reset();
		Visited.AddZeroed(NumNonDisabledParticles);
	}
	else
	{
		if (!(NumNonDisabledParticles <= Nodes.Num())) 
		{
			for (auto& Particle : Particles)
			{
				if(!ParticleToNodeIndex.Contains(Particle.Handle()))
					ParticleAdd(Particle.Handle());
			}
		}

		// Update nodes may contain duplicate entries. To process in parallel we either need to prevent that, 
		// or ensure we only process the first entry of a dupe. We are doing the latter for now...
		UpdatedNodes.Sort();
		ParallelFor(UpdatedNodes.Num(), 
			[&](int32 Index)
			{
				if ((Index == 0) || (UpdatedNodes[Index] != UpdatedNodes[Index - 1]))
				{
					Nodes[UpdatedNodes[Index]].Island = INDEX_NONE;
					Nodes[UpdatedNodes[Index]].Edges.Empty();
					auto* Particle = Nodes[UpdatedNodes[Index]].Particle;
				    if (CHAOS_ENSURE(Particle))
				    {
						// This does NOT check if particle is actually dynamic. TODO confirm that this should happen to all nonDisabledParticles, not just dynamics.
						FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle();
						if(PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
					    {
						    PBDRigid->Island() = INDEX_NONE;
					    }
				    }
				}
			});
		UpdatedNodes.Empty();

	}

	Edges.Reset();

	//@todo(ocohen): Should we reset more than just the edges? What about bIsIslandPersistant?
	for (TArray<int32>& IslandConstraintList : IslandToConstraints)
	{
		IslandConstraintList.Reset();
	}
}


void FPBDConstraintGraph::ResetIslands(const TParticleView<FPBDRigidParticles>& PBDRigids)
{
	//@todo(ocohen): Should we reset more than just the edges? What about bIsIslandPersistant?
	for (TArray<int32>& IslandConstraintList : IslandToConstraints)
	{
		IslandConstraintList.Reset();
	}

	for(auto& PBDRigid : PBDRigids)	//@todo(ocohen): could go wide per island if we can get at the sets
	{
		const int32 Island = PBDRigid.Island();
		if (Island >= 0)
		{
			FGraphNode& Node = Nodes[ParticleToNodeIndex[PBDRigid.Handle()]];
			Node.Island = Island;
			for (int32 ConstraintDataIndex : Node.Edges)
			{
				IslandToConstraints[Island].Add(ConstraintDataIndex);
			}
		}
	}
}


void FPBDConstraintGraph::ReserveConstraints(const int32 NumConstraints)
{
	Edges.Reserve(Edges.Num() + NumConstraints);
}


void FPBDConstraintGraph::AddConstraint(const uint32 InContainerId, FConstraintHandle* InConstraintHandle, const TVec2<FGeometryParticleHandle*>& ConstrainedParticles)
{
	// Must have at least one constrained particle
	check((ConstrainedParticles[0]) || (ConstrainedParticles[1]));

	const int32 NewEdgeIndex = Edges.Num();
	FGraphEdge NewEdge;
	NewEdge.Data = { InContainerId, InConstraintHandle };

	if (ConstrainedParticles[0] && !ParticleToNodeIndex.Contains(ConstrainedParticles[0]))
	{
		ParticleAdd(ConstrainedParticles[0]);
	}
	if (ConstrainedParticles[1] && !ParticleToNodeIndex.Contains(ConstrainedParticles[1]))
	{
		ParticleAdd(ConstrainedParticles[1]);
	}

	int32* PNodeIndex0 = (ConstrainedParticles[0])? ParticleToNodeIndex.Find(ConstrainedParticles[0]) : nullptr;
	int32* PNodeIndex1 = (ConstrainedParticles[1])? ParticleToNodeIndex.Find(ConstrainedParticles[1]) : nullptr;
	if (ensure(PNodeIndex0 || PNodeIndex1))
	{
		if (PNodeIndex0)
		{
			NewEdge.FirstNode = *PNodeIndex0;
			Nodes[NewEdge.FirstNode].Particle = ConstrainedParticles[0];
			Nodes[NewEdge.FirstNode].Edges.Add(NewEdgeIndex);
			UpdatedNodes.Add(NewEdge.FirstNode);
		}
		if (PNodeIndex1)
		{
			NewEdge.SecondNode = *PNodeIndex1;
			Nodes[NewEdge.SecondNode].Particle = ConstrainedParticles[1];
			Nodes[NewEdge.SecondNode].Edges.Add(NewEdgeIndex);
			UpdatedNodes.Add(NewEdge.SecondNode);
		}

		Edges.Add(MoveTemp(NewEdge));
	}
}


void FPBDConstraintGraph::RemoveConstraint(const uint32 InContainerId, FConstraintHandle* InConstraintHandle, const TVector<FGeometryParticleHandle*, 2>& ConstrainedParticles)
{
	check(InConstraintHandle);

	int32* PNodeIndex0 = (ConstrainedParticles[0]) ? ParticleToNodeIndex.Find(ConstrainedParticles[0]) : nullptr;
	int32* PNodeIndex1 = (ConstrainedParticles[1]) ? ParticleToNodeIndex.Find(ConstrainedParticles[1]) : nullptr;
	if (ensure(PNodeIndex0 || PNodeIndex1) )
	{
		if ( InContainerId < (uint32)Edges.Num() )
		{
			int32 Index0 = INDEX_NONE, Index1 = INDEX_NONE;
			if (PNodeIndex0) 
			{
				Nodes[*PNodeIndex0].Edges.Remove(InContainerId);
				Index0 = *PNodeIndex0;
			}
			if (PNodeIndex1)
			{
				Nodes[*PNodeIndex1].Edges.Remove(InContainerId);
				Index1 = *PNodeIndex1;
			}
			if (Edges[InContainerId].FirstNode == Index0 && Edges[InContainerId].SecondNode == Index1)
			{
				Edges[InContainerId] = FGraphEdge();
			}
		}
	}
}

const typename FPBDConstraintGraph::FConstraintData& FPBDConstraintGraph::GetConstraintData(int32 ConstraintDataIndex) const
{
	return Edges[ConstraintDataIndex].Data;
}


void FPBDConstraintGraph::UpdateIslands(const TParticleView<FPBDRigidParticles>& PBDRigids, FPBDRigidsSOAs& Particles)
{
	// Maybe expose a memset style function for this instead of iterating
	for (auto& PBDRigid : PBDRigids)
	{
		PBDRigid.Island() = INDEX_NONE;
		// When enabling particle from a break, if the object state is static, then when Enabled the paticle doesn't get added
		// to the constraint graph however the particle appears in GetNonDisabledDynamicView() so this ensure fires
		if (!/*ensure*/(ParticleToNodeIndex.Contains(PBDRigid.Handle())))
		{
			ParticleAdd(PBDRigid.Handle());
		}
	}
	ComputeIslands(PBDRigids, Particles);
}


DECLARE_CYCLE_STAT(TEXT("IslandGeneration2"), STAT_IslandGeneration2, STATGROUP_Chaos);


void FPBDConstraintGraph::ComputeIslands(const TParticleView<FPBDRigidParticles>& PBDRigids, FPBDRigidsSOAs& Particles)
{
	SCOPE_CYCLE_COUNTER(STAT_IslandGeneration2);

	int32 NextIsland = 0;
	TArray<TSet<FGeometryParticleHandle*>> NewIslandParticles;
	TArray<int32> NewIslandToSleepCount;

	VisitToken++;
	if (VisitToken==0)
	{
		VisitToken++;
	}

	// Instead of iterating over every node to reset Island, only iterate over the ones we care about for the following ComputeIslands algorithm to work 
	ParallelFor(Edges.Num(), [&](int32 Idx)
	{
		const FGraphEdge& Edge = Edges[Idx];
		Nodes[Edge.FirstNode].Island = INDEX_NONE;
		if (Edge.SecondNode != -1)
		{
			Nodes[Edge.SecondNode].Island = INDEX_NONE;
		}
	});

	IslandToData.Reset();

	for (auto& Particle : PBDRigids)
	{
		auto* ParticleHandle = Particle.Handle();
		int32 Idx = ParticleToNodeIndex[ParticleHandle];  // ryan - FAILS!
		// selective reset of islands, don't reset if has been visited due to being edge connected to earlier processed node
		if (Visited[Idx] && Visited[Idx] != VisitToken)
		{
			Nodes[Idx].Island = INDEX_NONE;
			Visited[Idx] = VisitToken;
		}

		if (Nodes[Idx].Island >= 0)
		{
			// Island is already known - it was visited in ComputeIsland for a previous node
			continue;
		}

		TSet<FGeometryParticleHandle*> SingleIslandParticles;
		const bool bNeedsResim = ComputeIsland(Idx, NextIsland, SingleIslandParticles);

		if (SingleIslandParticles.Num())
		{
			NewIslandParticles.SetNum(NextIsland + 1);
			NewIslandParticles[NextIsland] = MoveTemp(SingleIslandParticles);
			NextIsland++;
			//if this is too slow when not doing resim, pass template in
			IslandToData.AddDefaulted();
			IslandToData.Last().bNeedsResim = bNeedsResim;
		}
	}

	check(IslandToData.Num() == NextIsland);
	IslandToConstraints.SetNum(NextIsland);

	for (int32 EdgeIndex = 0; EdgeIndex < Edges.Num(); ++EdgeIndex)
	{
		const FGraphEdge& Edge = Edges[EdgeIndex];
		int32 FirstIsland = (Edge.FirstNode != INDEX_NONE) ? Nodes[Edge.FirstNode].Island : INDEX_NONE;
		int32 SecondIsland = (Edge.SecondNode != INDEX_NONE) ? Nodes[Edge.SecondNode].Island : INDEX_NONE;
		check(FirstIsland == SecondIsland || FirstIsland == INDEX_NONE || SecondIsland == INDEX_NONE);

		int32 Island = (FirstIsland != INDEX_NONE) ? FirstIsland : SecondIsland;
		
		// @todo(ccaulfield): should check(Island >= 0) when we disable particles properly
		if (Island >= 0)
		{
			IslandToConstraints[Island].Add(EdgeIndex);
		}
	}

	NewIslandToSleepCount.SetNum(NewIslandParticles.Num());

	if (NewIslandParticles.Num())
	{
		for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
		{
			NewIslandToSleepCount[Island] = 0;
			const bool bNeedsResim = IslandToData[Island].bNeedsResim;
			for (FGeometryParticleHandle* Particle : NewIslandParticles[Island])
			{
				FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle();
				if (PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
				{
					PBDRigid->Island() = Island;
					if(bNeedsResim)
					{
						if(PBDRigid->SyncState() == ESyncState::InSync)
						{
							//mark as soft desync, we may end up with exact same output
							PBDRigid->SetSyncState(ESyncState::SoftDesync);
						}
					}
				}
			}
		}
		// Force consistent state if no previous islands
		if (!IslandToParticles.Num())
		{
			for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
			{
				IslandToData[Island].bIsIslandPersistant = true;
				bool bSleepState = true;

				for (FGeometryParticleHandle* Particle : NewIslandParticles[Island])
				{
					if (Particle->ObjectState() != EObjectStateType::Static && !Particle->Sleeping())
					{
						bSleepState = false;
						break;
					}
				}

				for (FGeometryParticleHandle* Particle : NewIslandParticles[Island])
				{
					if (Particle->Sleeping() && !bSleepState)
					{
						Particles.ActivateParticle(Particle); 	//todo: record state change for array reorder
					}

					FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle();
					if(PBDRigid)
					{
						const EObjectStateType CurrState = PBDRigid->ObjectState();
						if(CurrState == EObjectStateType::Kinematic || CurrState == EObjectStateType::Static)
						{
							// Statics and kinematics can't have sleeping states so don't attempt to set one.
							break;
						}

						if (!Particle->Sleeping() && bSleepState)
						{
							Particles.DeactivateParticle(Particle); 	//todo: record state change for array reorder
							PBDRigid->V() = FVec3(0);
							PBDRigid->W() = FVec3(0);
						}

						PBDRigid->SetSleeping(bSleepState);
					}

					if (Particle->Sleeping())
					{
						Particles.DeactivateParticle(Particle);	//todo: record state change for array reorder (function could return true/false)
					}
				}
			}
		}

		for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
		{
			bool bIsSameIsland = true;

			// Non-kinematic particles were removed from the island
			int32 OtherIsland = -1;

			for (FGeometryParticleHandle* Particle : IslandToParticles[Island])
			{
				if (CHAOS_ENSURE(Particle))
				{
					FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle();

					if (PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Kinematic && PBDRigid->V().SizeSquared() > 0)
					{
						bIsSameIsland = false;
						break;
					}

					const bool bIsDynamic = PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic;

					if (bIsDynamic && PBDRigid->PreObjectState() == EObjectStateType::Kinematic)
					{
						bIsSameIsland = false;
						break;
					}
					
					int32 TmpIsland = bIsDynamic ? PBDRigid->Island() : INDEX_NONE; //question: should we even store non dynamics in this array?

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
			}

			// Kinematic particles were removed from the island. This needs to be called after OtherIsland is available.
			if (bIsSameIsland && OtherIsland >= 0)
			{
				for (FGeometryParticleHandle* Particle : IslandToParticles[Island])
				{
					if (CHAOS_ENSURE(Particle))
					{
						FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle();

						// If an island has many kinematic particles, this could be slow.
						if (PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Kinematic && !NewIslandParticles[OtherIsland].Contains(PBDRigid))
						{
							bIsSameIsland = false;
							break;
						}
					}
				}
			}

			// A new object entered the island or the island is entirely new particles
			if (bIsSameIsland && (OtherIsland == INDEX_NONE || NewIslandParticles[OtherIsland].Num() != IslandToParticles[Island].Num()))
			{
				bIsSameIsland = false;
			}

			// Find out if we need to activate island
			if (bIsSameIsland)
			{
				NewIslandToSleepCount[OtherIsland] = IslandToSleepCount[Island];
			}
			else
			{
				for (TGeometryParticleHandle<FReal, 3>* Particle : IslandToParticles[Island])
				{
					if (CHAOS_ENSURE(Particle))
					{
						TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
						if (PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
						{
							Particles.ActivateParticle(Particle);
						}
					}
				}
			}

			// #BG Necessary? Should we ever not find an island?
			if (OtherIsland != INDEX_NONE)
			{
				IslandToData[OtherIsland].bIsIslandPersistant = bIsSameIsland;
			}
		}
	}

	IslandToParticles.Reset();
	IslandToParticles.Reserve(NewIslandParticles.Num());
	for (int32 Island = 0; Island < NewIslandParticles.Num(); ++Island)
	{
		IslandToParticles.Emplace(NewIslandParticles[Island].Array());
	}
	IslandToSleepCount = MoveTemp(NewIslandToSleepCount);

	check(IslandToParticles.Num() == IslandToSleepCount.Num());
	check(IslandToParticles.Num() == IslandToConstraints.Num());
	check(IslandToParticles.Num() == IslandToData.Num());
	// @todo(ccaulfield): make a more complex unit test to check island integrity
	//checkSlow(CheckIslands(InParticles, ActiveIndices));
}


bool FPBDConstraintGraph::ComputeIsland(const int32 InNode, const int32 Island, TSet<FGeometryParticleHandle *>& ParticlesInIsland)
{
	TQueue<int32> NodeQueue;
	NodeQueue.Enqueue(InNode);
	bool bIslandNeedsToResim = false;
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

		if(!bIslandNeedsToResim)
		{
			//if even one particle is soft/hard desync we must resim the entire island (when resim is used)
			//seems cheap enough so just always do it, if slow pass resim template in here
			bIslandNeedsToResim = Node.Particle->SyncState() != ESyncState::InSync;
		}

		FPBDRigidParticleHandle* RigidHandle = Node.Particle->CastToRigidParticle();
		const bool isRigidDynamic = RigidHandle && RigidHandle->ObjectState() != EObjectStateType::Kinematic;  //??

		ParticlesInIsland.Add(Node.Particle);
		if (isRigidDynamic == false)
		{
			continue;
		}

		// @todo(ccaulfield): we don't handle enable/disable properly so this breaks
		//if (RigidHandle->Disabled())
		//{
		//	continue;
		//}

		Node.Island = Island;
		Visited[NodeIndex] = VisitToken;

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

	return bIslandNeedsToResim;
}

bool FPBDConstraintGraph::SleepInactive(const int32 Island, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterialAttributes, const THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials)
{
	if (!ChaosSolverSleepEnabled)
	{
		return false;
	}

	FReal LinearSleepingThreshold = FLT_MAX;
	FReal AngularSleepingThreshold = FLT_MAX;
	int32 SleepCounterThreshold = 0;

	const TArray<FGeometryParticleHandle*>& IslandParticles = GetIslandParticles(Island);
	check(IslandParticles.Num());

	if (!IslandToData[Island].bIsIslandPersistant)
	{
		return false;
	}

	int32& IslandSleepCount = IslandToSleepCount[Island];

	FReal MaxLinearSpeed2 = 0.f;
	FReal MaxAngularSpeed2 = 0.f;
	int32 NumDynamicParticles = 0;

	for (FGeometryParticleHandle* Particle : IslandParticles)
	{
		if (FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle())
		{
			if (PBDRigid->ObjectState() == EObjectStateType::Dynamic)
			{
				NumDynamicParticles++;

				const FReal LinearSpeed2 = PBDRigid->VSmooth().SizeSquared();
				MaxLinearSpeed2 = FMath::Max(LinearSpeed2,MaxLinearSpeed2);

				const FReal AngularSpeed2 = PBDRigid->WSmooth().SizeSquared();
				MaxAngularSpeed2 = FMath::Max(AngularSpeed2,MaxAngularSpeed2);

				bool bThresholdsSet = false;
				if (TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial = Particle->AuxilaryValue(PerParticleMaterialAttributes))
				{
					LinearSleepingThreshold = FMath::Min(LinearSleepingThreshold, PhysicsMaterial->SleepingLinearThreshold);
					AngularSleepingThreshold = FMath::Min(AngularSleepingThreshold, PhysicsMaterial->SleepingAngularThreshold);
					SleepCounterThreshold = FMath::Max(SleepCounterThreshold, PhysicsMaterial->SleepCounterThreshold);
					bThresholdsSet = true;
				}
				else if (PBDRigid->ShapesArray().Num())
				{
					if (FPerShapeData* PerShapeData = PBDRigid->ShapesArray()[0].Get())
					{
						if (PerShapeData->GetMaterials().Num())
						{
							if (FChaosPhysicsMaterial* Material = SolverPhysicsMaterials.Get(PBDRigid->ShapesArray()[0].Get()->GetMaterials()[0].InnerHandle))
							{
								LinearSleepingThreshold = FMath::Min(LinearSleepingThreshold, Material->SleepingLinearThreshold);
								AngularSleepingThreshold = FMath::Min(AngularSleepingThreshold, Material->SleepingAngularThreshold);
								SleepCounterThreshold = FMath::Max(SleepCounterThreshold, Material->SleepCounterThreshold);
								bThresholdsSet = true;
							}
						}
					}
				}

				if (!bThresholdsSet)
				{
					LinearSleepingThreshold = FMath::Min(LinearSleepingThreshold, ChaosSolverCollisionDefaultLinearSleepThresholdCVar);
					AngularSleepingThreshold = FMath::Min(AngularSleepingThreshold, ChaosSolverCollisionDefaultAngularSleepThresholdCVar);
					SleepCounterThreshold = FMath::Max(SleepCounterThreshold, ChaosSolverCollisionDefaultSleepCounterThresholdCVar);
				}
			}
		}
	}

	if (NumDynamicParticles == 0)
	{
		// prevent divide by zero - all particles much be sleeping/disabled already
		return false;
	}

	const FReal MaxLinearSpeed = FMath::Sqrt(MaxLinearSpeed2);
	const FReal MaxAngularSpeed = FMath::Sqrt(MaxAngularSpeed2);

	if (MaxLinearSpeed < LinearSleepingThreshold && MaxAngularSpeed < AngularSleepingThreshold)
	{
		if (IslandSleepCount >= SleepCounterThreshold)
		{
			return true;
		}
		else
		{
			IslandSleepCount++;
		}
	}
	else
	{
		//Reset sleep count since island is awake
		IslandSleepCount = 0;
	}

	return false;
}


void FPBDConstraintGraph::WakeIsland(FPBDRigidsSOAs& Particles, const int32 Island)
{
	if (Island < IslandToParticles.Num())
	{
		for (FGeometryParticleHandle * Particle : IslandToParticles[Island])
		{
			FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle();
			if (PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
			{
				if (PBDRigid->Sleeping())
				{
					PBDRigid->SetSleeping(false);
					Particles.EnableParticle(Particle);
				}
			}
		}
		IslandToSleepCount[Island] = 0;
	}
}


/*
void FPBDConstraintGraph::ReconcileIslands()
{
	for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
	{
		bool IsSleeping = true;
		bool IsSet = false;
		for (FGeometryParticleHandle* Particle : IslandToParticles[Island])
		{
			if (Particle->ObjectState() == EObjectStateType::Static)
			{
				continue;
			}
			if (!IsSet)
			{
				IsSet = true;
				IsSleeping = Particle->Sleeping();
			}
			if (Particle->Sleeping() != IsSleeping)
			{
				WakeIsland(Island);
				break;
			}
		}
	}
}
*/

void FPBDConstraintGraph::EnableParticle(FGeometryParticleHandle* Particle, const FGeometryParticleHandle* ParentParticle)
{
	if (ParentParticle)
	{
		const FPBDRigidParticleHandle* ParentPBDRigid = ParentParticle->CastToRigidParticle();
		if(ParentPBDRigid && ParentPBDRigid->ObjectState() == EObjectStateType::Dynamic)
		{
			ParticleAdd(Particle);

			FPBDRigidParticleHandle* ChildPBDRigid = Particle->CastToRigidParticle();
			if(ChildPBDRigid && ChildPBDRigid->ObjectState() == EObjectStateType::Dynamic)
			{
				const int32 Island = ParentPBDRigid->Island();
				ChildPBDRigid->Island() = Island;

				// If our parent had a valid island, add the child to it.
				if (IslandToParticles.IsValidIndex(Island))
				{
					IslandToParticles[Island].Add(Particle);
				}

				const bool SleepState = ParentPBDRigid->Sleeping();
				ChildPBDRigid->SetSleeping(SleepState);	//todo: need to let evolution know to reorder arrays
			}
			else
			{
				ensure(false);	//this should never happen
			}
		}
	}
}


void FPBDConstraintGraph::DisableParticle(FGeometryParticleHandle* Particle)
{
	FPBDRigidParticleHandle* PBDRigid = Particle->CastToRigidParticle();
	if(PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
	{
		const int32 Island = PBDRigid->Island();
		if (Island != INDEX_NONE)
		{
			PBDRigid->Island() = INDEX_NONE;

			// @todo(ccaulfield): optimize
			if (ensure(IslandToParticles.IsValidIndex(Island)))
			{
				int32 IslandParticleArrayIdx = IslandToParticles[Island].Find(Particle);
				check(IslandParticleArrayIdx != INDEX_NONE);
				IslandToParticles[Island].RemoveAtSwap(IslandParticleArrayIdx);
			}
		}

	}
	else
	{
		// Kinematic & Static particles are included in IslandToParticles, however we cannot use islands to look them up.
		// TODO find faster removal method?
		for (TArray<FGeometryParticleHandle*>& IslandParticles : IslandToParticles)
		{
			IslandParticles.RemoveSingleSwap(Particle);
		}
	}

	ParticleRemove(Particle);
}


void FPBDConstraintGraph::DisableParticles(const TSet<FGeometryParticleHandle *>& Particles)
{
	// @todo(ccaulfield): optimize
	for (FGeometryParticleHandle* Particle : Particles)
	{
		DisableParticle(Particle);
	}
}


bool FPBDConstraintGraph::CheckIslands(const TArray<FGeometryParticleHandle *>& Particles)
{
	bool bIsValid = true;

	// Check that no particles are in multiple islands
	TSet<FGeometryParticleHandle*> IslandParticlesUnionSet;
	IslandParticlesUnionSet.Reserve(Particles.Num());
	for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
	{
		TSet<FGeometryParticleHandle*> IslandParticlesSet = TSet<FGeometryParticleHandle*>(IslandToParticles[Island]);
		TSet<FGeometryParticleHandle*> IslandParticlesIntersectSet = IslandParticlesUnionSet.Intersect(IslandParticlesSet);
		if (IslandParticlesIntersectSet.Num() > 0)
		{
			// This islands contains particles that were in a previous island.
			// This is ok only if those particles are static
			for (FGeometryParticleHandle* Particle : IslandParticlesIntersectSet)
			{
				if (Particle->CastToRigidParticle() && Particle->ObjectState() == EObjectStateType::Dynamic)
				{
					UE_LOG(LogChaos, Error, TEXT("Island %d contains non-static particle that is also in another Island"), Island);	//todo: add better logging for bad particle
					bIsValid = false;
				}
			}
		}
		IslandParticlesUnionSet = IslandParticlesUnionSet.Union(IslandParticlesSet);
	}

	// Check that no constraints refer in the same island
	TSet<int32> IslandConstraintDataUnionSet;
	IslandConstraintDataUnionSet.Reserve(Edges.Num());
	for (int32 Island = 0; Island < IslandToConstraints.Num(); ++Island)
	{
		TSet<int32> IslandConstraintDataSet = TSet<int32>(IslandToConstraints[Island]);
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
