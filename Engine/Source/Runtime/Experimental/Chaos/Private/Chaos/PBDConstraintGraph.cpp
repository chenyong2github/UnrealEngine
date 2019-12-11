// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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



FPBDConstraintGraph::FPBDConstraintGraph() : VisitToken(0)
{
}


FPBDConstraintGraph::FPBDConstraintGraph(const TParticleView<TGeometryParticles<FReal, 3>>& Particles) : VisitToken(0)
{
	InitializeGraph(Particles);
}

/**
 * Bill added this.
 * Adds new Node to Nodes array when a new particle is created
 */

void FPBDConstraintGraph::ParticleAdd(TGeometryParticleHandle<FReal, 3>* AddedParticle)
{
	int32 NewNodeIndex = GetNextNodeIndex();

	FGraphNode& Node = Nodes[NewNodeIndex];
	ensure(Node.Edges.Num() == 0);
	ensure(Node.Island == INDEX_NONE);
	ensure(ParticleToNodeIndex.Find(AddedParticle) == nullptr);

	Node.Particle = AddedParticle->Handle();
	ParticleToNodeIndex.Add(Node.Particle, NewNodeIndex);
	Visited.Add(0);
}

/**
 * Bill added this
 * Removes Node from Nodes array - marking it an unused, also clears ParticleToNodeIndex
 */

void FPBDConstraintGraph::ParticleRemove(TGeometryParticleHandle<FReal, 3>* RemovedParticle)
{
	if (ParticleToNodeIndex.Contains(RemovedParticle))
	{
		int32 NodeIdx = ParticleToNodeIndex[RemovedParticle];
		FreeIndexList.Push(NodeIdx);

		FGraphNode& NodeRemoved = Nodes[NodeIdx];
		NodeRemoved.Edges.Empty();
		NodeRemoved.Particle = nullptr;
		NodeRemoved.Island = INDEX_NONE;

		Visited[NodeIdx]=0;
		ParticleToNodeIndex.Remove(RemovedParticle);
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

void FPBDConstraintGraph::InitializeGraph(const TParticleView<TGeometryParticles<FReal, 3>>& Particles)
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
		ensure(NumNonDisabledParticles <= Nodes.Num());

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
						TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
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


void FPBDConstraintGraph::ResetIslands(const TParticleView<TPBDRigidParticles<FReal, 3>>& PBDRigids)
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


void FPBDConstraintGraph::AddConstraint(const uint32 InContainerId, FConstraintHandle* InConstraintHandle, const TVector<TGeometryParticleHandle<FReal, 3>*, 2>& ConstrainedParticles)
{
	// Must have at least one constrained particle
	check((ConstrainedParticles[0]) || (ConstrainedParticles[1]));

	const int32 NewEdgeIndex = Edges.Num();
	FGraphEdge NewEdge;
	NewEdge.Data = { InContainerId, InConstraintHandle };

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


const typename FPBDConstraintGraph::FConstraintData& FPBDConstraintGraph::GetConstraintData(int32 ConstraintDataIndex) const
{
	return Edges[ConstraintDataIndex].Data;
}


void FPBDConstraintGraph::UpdateIslands(const TParticleView<TPBDRigidParticles<FReal, 3>>& PBDRigids, TPBDRigidsSOAs<FReal, 3>& Particles)
{
	// Maybe expose a memset style function for this instead of iterating
	for (auto& PBDRigid : PBDRigids)
	{
		PBDRigid.Island() = INDEX_NONE;
	}
	ComputeIslands(PBDRigids, Particles);
}


DECLARE_CYCLE_STAT(TEXT("IslandGeneration2"), STAT_IslandGeneration2, STATGROUP_Chaos);


void FPBDConstraintGraph::ComputeIslands(const TParticleView<TPBDRigidParticles<FReal, 3>>& PBDRigids, TPBDRigidsSOAs<FReal, 3>& Particles)
{
	SCOPE_CYCLE_COUNTER(STAT_IslandGeneration2);

	int32 NextIsland = 0;
	TArray<TSet<TGeometryParticleHandle<FReal, 3>*>> NewIslandParticles;
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

	for (auto& Particle : PBDRigids)
	{
		int32 Idx = ParticleToNodeIndex[Particle.Handle()];
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

		TSet<TGeometryParticleHandle<FReal, 3>*> SingleIslandParticles;
		TSet<TGeometryParticleHandle<FReal, 3>*> SingleIslandStaticParticles;
		ComputeIsland(Idx, NextIsland, SingleIslandParticles, SingleIslandStaticParticles);

		for (TGeometryParticleHandle<FReal, 3>* StaticParticle : SingleIslandStaticParticles)
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

	IslandToConstraints.SetNum(NextIsland);
	IslandToData.SetNum(NextIsland);

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

			for (TGeometryParticleHandle<FReal, 3>* Particle : NewIslandParticles[Island])
			{
				TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
				if (PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
				{
					PBDRigid->Island() = Island;
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

				for (TGeometryParticleHandle<FReal, 3>* Particle : NewIslandParticles[Island])
				{
					if (!Particle->Sleeping())
					{
						bSleepState = false;
						break;
					}
				}

				for (TGeometryParticleHandle<FReal, 3>* Particle : NewIslandParticles[Island])
				{
					//@todo(DEMO_HACK) : Need to fix, remove the !InParticles.Disabled(Index)
					if (Particle->Sleeping() && !bSleepState/* && !Particle->Disabled()*/)
					{
						Particles.ActivateParticle(Particle); 	//todo: record state change for array reorder
					}

					TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
					if(PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
					{
						if (!Particle->Sleeping() && bSleepState)
						{
							Particles.DeactivateParticle(Particle); 	//todo: record state change for array reorder
							PBDRigid->V() = TVector<FReal, 3>(0);
							PBDRigid->W() = TVector<FReal, 3>(0);
						}

						PBDRigid->SetSleeping(bSleepState);
					}

					if ((Particle->Sleeping() /*|| Particle->Disabled()*/))
					{
						Particles.DeactivateParticle(Particle);	//todo: record state change for array reorder (function could return true/false)
					}
				}
			}
		}

		for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
		{
			bool bIsSameIsland = true;

			// Objects were removed from the island
			int32 OtherIsland = -1;

			for (TGeometryParticleHandle<FReal, 3>* Particle : IslandToParticles[Island])
			{
				TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
				const bool bIsDynamic = PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic;
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
					TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
					if (PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
					{
						if (!PBDRigid->Disabled())	// todo: why is this needed? [we aren't handling enable/disable state changes properly so disabled particles end up in the graph.]
						{
							PBDRigid->SetSleeping(false);
							Particles.ActivateParticle(Particle);
						}
					}
					else
					{
						Particles.ActivateParticle(Particle);
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


void FPBDConstraintGraph::ComputeIsland(const int32 InNode, const int32 Island, TSet<TGeometryParticleHandle<FReal, 3> *>& DynamicParticlesInIsland,
	TSet<TGeometryParticleHandle<FReal, 3> *>& StaticParticlesInIsland)
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

		TPBDRigidParticleHandle<FReal, 3>* RigidHandle = Node.Particle->CastToRigidParticle();
		const bool isRigidDynamic = RigidHandle && RigidHandle->ObjectState() != EObjectStateType::Kinematic;  //??

		if (isRigidDynamic == false)
		{
			if (!StaticParticlesInIsland.Contains(Node.Particle))
			{
				StaticParticlesInIsland.Add(Node.Particle);
			}
			continue;
		}

		// @todo(ccaulfield): we don't handle enable/disable properly so this breaks
		//if (RigidHandle->Disabled())
		//{
		//	continue;
		//}

		DynamicParticlesInIsland.Add(Node.Particle);
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
}


bool FPBDConstraintGraph::SleepInactive(const int32 Island, const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterialAttributes)
{
	// @todo(ccaulfield): should be able to eliminate this when island is already sleeping

	const TArray<TGeometryParticleHandle<FReal, 3>*>& IslandParticles = GetIslandParticles(Island);
	check(IslandParticles.Num());

	if (!IslandToData[Island].bIsIslandPersistant)
	{
		return false;
	}

	int32& IslandSleepCount = IslandToSleepCount[Island];

	TVector<FReal, 3> V(0);
	TVector<FReal, 3> W(0);
	FReal M = 0;
	FReal LinearSleepingThreshold = FLT_MAX;
	FReal AngularSleepingThreshold = FLT_MAX;
	FReal DefaultLinearSleepingThreshold = (FReal)1;
	FReal DefaultAngularSleepingThreshold = (FReal)1;

	int32 NumDynamicParticles = 0;

	for (const TGeometryParticleHandle<FReal, 3>* Particle : IslandToParticles[Island])
	{
		const TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
		if(PBDRigid && (PBDRigid->ObjectState() == EObjectStateType::Dynamic))
		{
			NumDynamicParticles++;

			M += PBDRigid->M();
			V += PBDRigid->V() * PBDRigid->M();

			if (TSerializablePtr<FChaosPhysicsMaterial> PhysicsMaterial = Particle->AuxilaryValue(PerParticleMaterialAttributes))
			{
				LinearSleepingThreshold = FMath::Min(LinearSleepingThreshold, PhysicsMaterial->SleepingLinearThreshold);
				AngularSleepingThreshold = FMath::Min(AngularSleepingThreshold, PhysicsMaterial->SleepingAngularThreshold);
			}
			else
			{
				LinearSleepingThreshold = DefaultLinearSleepingThreshold;
				AngularSleepingThreshold = DefaultAngularSleepingThreshold;
			}
		}
	}

	if (NumDynamicParticles == 0)
	{
		// prevent divide by zero - all particles much be sleeping/disabled already
		return false;
	}

	V /= M;

	for (const TGeometryParticleHandle<FReal, 3>* Particle: IslandParticles)
	{
		const TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
		if(PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
		{
			W += /*TVector<FReal, 3>::CrossProduct(PBDRigid->X() - X, PBDRigid->M() * PBDRigid->V()/ +*/ PBDRigid->W() * PBDRigid->M();
		}
	}

	W /= M;

	const FReal VSize = V.SizeSquared();
	const FReal WSize = W.SizeSquared();
	if (VSize < LinearSleepingThreshold && WSize < AngularSleepingThreshold)
	{
		if (IslandSleepCount > SleepCountThreshold)
		{
			for (TGeometryParticleHandle<FReal, 3>* Particle : IslandParticles)
			{
				TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
				if(PBDRigid && PBDRigid->ObjectState() == EObjectStateType::Dynamic)
				{
					PBDRigid->SetSleeping(true);
					PBDRigid->V() = TVector<FReal, 3>(0);
					PBDRigid->W() = TVector<FReal, 3>(0);
				}
				
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


void FPBDConstraintGraph::WakeIsland(const int32 Island)
{
	for (TGeometryParticleHandle<FReal, 3>* Particle : IslandToParticles[Island])
	{
		TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
		if(PBDRigid && PBDRigid->ObjectState() != EObjectStateType::Kinematic)
		{
			if (PBDRigid->Sleeping())
			{
				PBDRigid->SetSleeping(false);
			}
		}
	}
	IslandToSleepCount[Island] = 0;
}



void FPBDConstraintGraph::ReconcileIslands()
{
	for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
	{
		bool IsSleeping = true;
		bool IsSet = false;
		for (TGeometryParticleHandle<FReal, 3>* Particle : IslandToParticles[Island])
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


void FPBDConstraintGraph::EnableParticle(TGeometryParticleHandle<FReal, 3>* Particle, const TGeometryParticleHandle<FReal, 3>* ParentParticle)
{
	if (ParentParticle)
	{
		const TPBDRigidParticleHandle<FReal, 3>* ParentPBDRigid = ParentParticle->CastToRigidParticle();
		if(ParentPBDRigid && ParentPBDRigid->ObjectState() == EObjectStateType::Dynamic)
		{
			ParticleAdd(Particle);

			TPBDRigidParticleHandle<FReal, 3>* ChildPBDRigid = Particle->CastToRigidParticle();
			if(ChildPBDRigid && ChildPBDRigid->ObjectState() == EObjectStateType::Dynamic)
			{
				const int32 Island = ParentPBDRigid->Island();
				ChildPBDRigid->Island() = Island;
				if (ensure(IslandToParticles.IsValidIndex(Island)))
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


void FPBDConstraintGraph::DisableParticle(TGeometryParticleHandle<FReal, 3>* Particle)
{
	TPBDRigidParticleHandle<FReal, 3>* PBDRigid = Particle->CastToRigidParticle();
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
	ParticleRemove(Particle);
}


void FPBDConstraintGraph::DisableParticles(const TSet<TGeometryParticleHandle<FReal, 3> *>& Particles)
{
	// @todo(ccaulfield): optimize
	for (TGeometryParticleHandle<FReal, 3>* Particle : Particles)
	{
		DisableParticle(Particle);
	}
}


bool FPBDConstraintGraph::CheckIslands(const TArray<TGeometryParticleHandle<FReal, 3> *>& Particles)
{
	bool bIsValid = true;

	// Check that no particles are in multiple islands
	TSet<TGeometryParticleHandle<FReal, 3>*> IslandParticlesUnionSet;
	IslandParticlesUnionSet.Reserve(Particles.Num());
	for (int32 Island = 0; Island < IslandToParticles.Num(); ++Island)
	{
		TSet<TGeometryParticleHandle<FReal, 3>*> IslandParticlesSet = TSet<TGeometryParticleHandle<FReal, 3>*>(IslandToParticles[Island]);
		TSet<TGeometryParticleHandle<FReal, 3>*> IslandParticlesIntersectSet = IslandParticlesUnionSet.Intersect(IslandParticlesSet);
		if (IslandParticlesIntersectSet.Num() > 0)
		{
			// This islands contains particles that were in a previous island.
			// This is ok only if those particles are static
			for (TGeometryParticleHandle<FReal, 3>* Particle : IslandParticlesIntersectSet)
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
