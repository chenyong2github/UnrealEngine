// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Island/IslandManager.h"
#include "Chaos/Island/SolverIsland.h"
#include "Chaos/Island/IslandGraph.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "ChaosLog.h"

#include "Framework/Threading.h"
#include "Misc/App.h"

/** Cvar to enable/disable the island sleeping */
bool ChaosSolverSleepEnabled = true;
FAutoConsoleVariableRef CVarChaosSolverSleepEnabled(TEXT("p.Chaos.Solver.SleepEnabled"), ChaosSolverSleepEnabled, TEXT(""));

/** Cvar to control the number of island groups used in the solver. The total number will be NumThreads * IslandGroupsMultiplier */
Chaos::FRealSingle GChaosSolverIslandGroupsMultiplier = 1;
FAutoConsoleVariableRef CVarSolverIslandGroupsMultiplier(TEXT("p.Chaos.Solver.IslandGroupsMultiplier"), GChaosSolverIslandGroupsMultiplier, TEXT("Total number of island groups in the solver will be NumThreads * IslandGroupsMultiplier.[def:1]"));

/** Cvar to override the sleep counter threshold if necessary */
int32 ChaosSolverCollisionDefaultSleepCounterThresholdCVar = 20;
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultSleepCounterThreshold(TEXT("p.ChaosSolverCollisionDefaultSleepCounterThreshold"), ChaosSolverCollisionDefaultSleepCounterThresholdCVar, TEXT("Default counter threshold for sleeping.[def:20]"));

/** Cvar to override the sleep linear threshold if necessary */
Chaos::FRealSingle ChaosSolverCollisionDefaultLinearSleepThresholdCVar = 0.001f; // .001 unit mass cm
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultLinearSleepThreshold(TEXT("p.ChaosSolverCollisionDefaultLinearSleepThreshold"), ChaosSolverCollisionDefaultLinearSleepThresholdCVar, TEXT("Default linear threshold for sleeping.[def:0.001]"));

/** Cvar to override the sleep angular threshold if necessary */
Chaos::FRealSingle ChaosSolverCollisionDefaultAngularSleepThresholdCVar = 0.0087f;  //~1/2 unit mass degree
FAutoConsoleVariableRef CVarChaosSolverCollisionDefaultAngularSleepThreshold(TEXT("p.ChaosSolverCollisionDefaultAngularSleepThreshold"), ChaosSolverCollisionDefaultAngularSleepThresholdCVar, TEXT("Default angular threshold for sleeping.[def:0.0087]"));

namespace Chaos
{
	extern int32 GSingleThreadedPhysics;
	
/** Check if a particle is dynamic or sleeping */
FORCEINLINE bool IsDynamicParticle(const FGeometryParticleHandle* ParticleHandle)
{
	return (ParticleHandle->ObjectState() == EObjectStateType::Dynamic) ||
		   (ParticleHandle->ObjectState() == EObjectStateType::Sleeping);
}

/** Check if a particle is not moving */
FORCEINLINE bool IsStationaryParticle(const FGeometryParticleHandle* ParticleHandle)
{
	if (ParticleHandle->ObjectState() == EObjectStateType::Kinematic)
	{
		const FKinematicGeometryParticleHandle* KinematicParticle = ParticleHandle->CastToKinematicParticle();
		return KinematicParticle->V().IsZero();
	}
	else
	{
		return (ParticleHandle->ObjectState() == EObjectStateType::Static) ||
			   (ParticleHandle->ObjectState() == EObjectStateType::Sleeping);
	}
}

/** Check if a particle is dynamic or sleeping */
inline const FChaosPhysicsMaterial* GetPhysicsMaterial(const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& ParticleMaterialAttributes,
														    const THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials, FPBDRigidParticleHandle* RigidParticleHandle)
{
	const FChaosPhysicsMaterial* PhysicsMaterial = RigidParticleHandle->AuxilaryValue(ParticleMaterialAttributes).Get();
	if (!PhysicsMaterial && RigidParticleHandle->ShapesArray().Num())
	{
		if (FPerShapeData* PerShapeData = RigidParticleHandle->ShapesArray()[0].Get())
		{
			if (PerShapeData->GetMaterials().Num())
			{
				PhysicsMaterial = SolverPhysicsMaterials.Get(PerShapeData->GetMaterials()[0].InnerHandle);
			}
		}
	}
	return PhysicsMaterial;
}

/** Check if an island is sleeping or not given a linear/angular velocities and sleeping thresholds */
inline bool IsIslandSleeping(const FReal MaxLinearSpeed2, const FReal MaxAngularSpeed2,
								  const FReal LinearSleepingThreshold, const FReal AngularSleepingThreshold,
								  const int32 CounterThreshold, int32& SleepCounter)
{
	const FReal MaxLinearSpeed = FMath::Sqrt(MaxLinearSpeed2);
	const FReal MaxAngularSpeed = FMath::Sqrt(MaxAngularSpeed2);

	if (MaxLinearSpeed < LinearSleepingThreshold && MaxAngularSpeed < AngularSleepingThreshold)
	{
		if (SleepCounter >= CounterThreshold)
		{
			return true;
		}
		else
		{
			SleepCounter++;
		}
	}
	else
	{
		SleepCounter = 0;
	}
	return false;
}

	
/**  Update all the island particles/constraints sleep state to be consistent with the island one */
inline void UpdateSleepState(FPBDIslandSolver* IslandSolver, FPBDRigidsSOAs& Particles)
{
	if(IslandSolver)
	{
		// Sleeping flag has already been computed by the island graph 
		const bool bIsSleeping = IslandSolver->IsSleeping();
		
		bool bNeedRebuild = false;
		for (auto& IslandParticle : IslandSolver->GetParticles())
		{
			if(IslandParticle->CastToRigidParticle() && !IslandParticle->CastToRigidParticle()->Disabled())
			{
				// If not sleeping we activate the sleeping particles
				if (!bIsSleeping && IslandParticle->Sleeping())
				{
					Particles.ActivateParticle(IslandParticle, true);

					// When we wake particles, we have skipped their integrate step which causes some issues:
					//	- we have zero velocity (no gravity or external forces applied)
					//	- the world transforms cached in the ShapesArray will be at the last post-integrate positions
					//	  which doesn't match what the velocity is telling us
					// This causes problems for the solver - essentially we have an "initial overlap" situation.
					// @todo(chaos): We could just run (partial) integrate here for this particle, but we don't know about the Evolution - fix this
					for (const TUniquePtr<FPerShapeData>& Shape : IslandParticle->ShapesArray())
					{
						Shape->UpdateLeafWorldTransform(IslandParticle);
					}

					bNeedRebuild = true;
				}
				// If sleeping we deactivate the dynamic particles
				else if (bIsSleeping && !IslandParticle->Sleeping())
				{
					Particles.DeactivateParticle(IslandParticle, true);
					bNeedRebuild = true;
				}
			}
		}
		if(bNeedRebuild)
		{
			Particles.RebuildViews();
		}
		// Island constraints are updating their sleeping flag + awaken one 
		for (auto& IslandConstraint : IslandSolver->GetConstraints())
		{
			IslandConstraint->SetIsSleeping(bIsSleeping);
		}
	}
}

/** Update all the island particles sync state to be consistent with the island one */
inline void UpdateSyncState(FPBDIslandSolver* IslandSolver, FPBDRigidsSOAs& Particles)
{
	if(IslandSolver)
	{
		bool bNeedsResim = false;
		for (auto& IslandParticle : IslandSolver->GetParticles())
		{
			//if even one particle is soft/hard desync we must resim the entire island (when resim is used)
			//seems cheap enough so just always do it, if slow pass resim template in here
			if (IslandParticle->SyncState() != ESyncState::InSync)
			{
				bNeedsResim = true;
				break;
			}
		}
		IslandSolver->SetNeedsResim(bNeedsResim);
	}
}
	/** Add all the graph particle and constraints to the solver islands*/
inline void PopulateIslands(FPBDIslandManager::GraphType* IslandGraph)
{
	auto AddNodeToIsland = [IslandGraph](const int32 IslandIndex, FPBDIslandManager::GraphType::FGraphNode& GraphNode)
	{
		if(IslandGraph->GraphIslands.IsValidIndex(IslandIndex))
		{
			FPBDIslandSolver* IslandSolver = IslandGraph->GraphIslands[IslandIndex].IslandItem;
			if(!IslandSolver->IsSleeping())
			{
				IslandSolver->AddParticle(GraphNode.NodeItem);
			}
		}
	};
	TSet<int32> NodeIslands;
	for(auto& GraphNode : IslandGraph->GraphNodes)
	{
		NodeIslands = GraphNode.NodeIslands;
		GraphNode.NodeIslands.Reset();

		// First we re-add the island indices coming from a sleeping islands since we are not adding them here.
		for (auto& IslandIndex : NodeIslands)
		{
			if (IslandGraph->GraphIslands.IsValidIndex(IslandIndex))
			{
				if (IslandGraph->GraphIslands[IslandIndex].IslandItem->IsSleeping())
				{
					GraphNode.NodeIslands.Add(IslandIndex);
				}
			}
		}
		// If the node is valid : only one island
		if( GraphNode.bValidNode)
		{
			AddNodeToIsland(GraphNode.IslandIndex, GraphNode);
			GraphNode.NodeIslands.Add(GraphNode.IslandIndex);
		}
		else
		{
			// A particle could belong to several islands when static/kinematic (not valid)
			// First we compute the unique particle set of islands
			for( auto& NodeEdge : GraphNode.NodeEdges)
			{
				GraphNode.NodeIslands.Add(IslandGraph->GraphEdges[NodeEdge].IslandIndex);
			}
			// Loop over the set of islands and add the particle to the solver island
			for(auto& NodeIsland : GraphNode.NodeIslands)
			{
				AddNodeToIsland(NodeIsland, GraphNode);
			}
		}
	}
	
	// Loop over the graph edges to transfer them into the solver island
	for(auto& GraphEdge : IslandGraph->GraphEdges)
	{
		if(IslandGraph->GraphIslands.IsValidIndex(GraphEdge.IslandIndex))
		{
			FPBDIslandSolver* IslandSolver = IslandGraph->GraphIslands[GraphEdge.IslandIndex].IslandItem;
			if(!IslandSolver->IsSleeping())
			{
				IslandSolver->AddConstraint(GraphEdge.EdgeItem);
			}
		}
	}
}
	
/** Compute sleeping thresholds given a solver island  */
inline bool ComputeSleepingThresholds(FPBDIslandSolver* IslandSolver,
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterialAttributes,
	const THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials, FReal& LinearSleepingThreshold,
	FReal& AngularSleepingThreshold, FReal& MaxLinearSpeed2, FReal& MaxAngularSpeed2, int32& SleepCounterThreshold)
{
	LinearSleepingThreshold = FLT_MAX;  
	AngularSleepingThreshold = FLT_MAX; 
	MaxLinearSpeed2 = 0.f; 
	MaxAngularSpeed2 = 0.f;
	SleepCounterThreshold = 0;

	bool bHaveSleepThreshold = false;
	for (FGeometryParticleHandle* ParticleHandle : IslandSolver->GetParticles())
	{
		if (ParticleHandle)
		{
			if (FPBDRigidParticleHandle* PBDRigid = ParticleHandle->CastToRigidParticle())
			{
				// Should we change this condition to be if (!IsStationaryParticle(ParticleHandle))
				// to be in sync with what is done for the graph island sleeping flag?
				if	(IsDynamicParticle(ParticleHandle) && !PBDRigid->Sleeping())
				{
					// If any body in the island is not allowed to sleep, the whole island cannot sleep
					// @todo(chaos): if this is a common thing, we should set a flag on the island when it has a particle
					// with this property enabled and skip the sleep check altogether
					if (PBDRigid->SleepType() == ESleepType::NeverSleep)
					{
						return false;
					}

					bHaveSleepThreshold = true;

					MaxLinearSpeed2 = FMath::Max(PBDRigid->VSmooth().SizeSquared(), MaxLinearSpeed2);
					MaxAngularSpeed2 = FMath::Max(PBDRigid->WSmooth().SizeSquared(), MaxAngularSpeed2);

					const FChaosPhysicsMaterial* PhysicsMaterial = GetPhysicsMaterial(PerParticleMaterialAttributes, SolverPhysicsMaterials, PBDRigid);

					const FReal LocalSleepingLinearThreshold = PhysicsMaterial ? PhysicsMaterial->SleepingLinearThreshold :
										ChaosSolverCollisionDefaultLinearSleepThresholdCVar;
					const FReal LocalAngularSleepingThreshold = PhysicsMaterial ? PhysicsMaterial->SleepingAngularThreshold :
										ChaosSolverCollisionDefaultAngularSleepThresholdCVar;
					const int32 LocalSleepCounterThreshold = PhysicsMaterial ? PhysicsMaterial->SleepCounterThreshold :
										ChaosSolverCollisionDefaultSleepCounterThresholdCVar;

					LinearSleepingThreshold = FMath::Min(LinearSleepingThreshold, LocalSleepingLinearThreshold);
					AngularSleepingThreshold = FMath::Min(AngularSleepingThreshold, LocalAngularSleepingThreshold);
					SleepCounterThreshold = FMath::Max(SleepCounterThreshold, LocalSleepCounterThreshold);
				}
			}
		}
	}
	return bHaveSleepThreshold;
}

FPBDIslandManager::FPBDIslandManager() : IslandSolvers(), IslandGraph(MakeUnique<GraphType>()), MaxParticleIndex(INDEX_NONE)
{
	InitializeGroups();
}

FPBDIslandManager::FPBDIslandManager(const TParticleView<FPBDRigidParticles>& PBDRigids) : IslandSolvers(), IslandGraph(MakeUnique<GraphType>())
{
	InitializeGraph(PBDRigids);
	InitializeGroups();
}

FPBDIslandManager::~FPBDIslandManager()
{}

void FPBDIslandManager::ResetIndices()
{
	// Reset of all the particles / constraint graph index to INDEX_NONE
	for(auto& ItemNode : IslandGraph->ItemNodes)
	{
		if(FPBDRigidParticleHandle* PBDRigid = ItemNode.Key->CastToRigidParticle())
		{
			PBDRigid->SetConstraintGraphIndex(INDEX_NONE);
		}
	}
	for(auto& ItemEdge : IslandGraph->ItemEdges)
	{
		ItemEdge.Key->SetConstraintGraphIndex(INDEX_NONE);
	}
}

void FPBDIslandManager::InitializeGroups()
{
	// @todo(chaos): is the number of worker threads a good indicator of how many threads we get in the solver loop? (Currently uses ParallelFor)
	// Check for use of the "-onethread" command line arg, and physics threading disabled (GetNumWorkerThreads() is not affected by these)
	const int32 NumWorkerThreads = (FApp::ShouldUseThreadingForPerformance() && !GSingleThreadedPhysics) ? FTaskGraphInterface::Get().GetNumWorkerThreads() : 0;
	const int32 MaxIslandGroups = FMath::Max(1, FMath::CeilToInt32(FReal(NumWorkerThreads) * GChaosSolverIslandGroupsMultiplier));

	IslandGroups.SetNum(MaxIslandGroups, false);
	
	for(int32 GroupIndex = 0, NumGroups = IslandGroups.Num(); GroupIndex < NumGroups; ++GroupIndex)
	{
		IslandGroups[GroupIndex] = MakeUnique<FPBDIslandGroup>(GroupIndex);
	}
}

void FPBDIslandManager::InitializeGraph(const TParticleView<FPBDRigidParticles>& PBDRigids)
{
	MaxParticleIndex = 0;
	ReserveParticles(PBDRigids.Num());
	// We are adding all the particles from the solver in
	// case some were just created/activated
	for (auto& RigidParticle : PBDRigids)
	{
		AddParticle(RigidParticle.Handle());
	}
	// we update the valid/steady state of the nodes in case any state changed
	for(int32 NodeIndex = 0, NumNodes = IslandGraph->GraphNodes.GetMaxIndex(); NodeIndex < NumNodes; ++NodeIndex)
	{
		if (IslandGraph->GraphNodes.IsValidIndex(NodeIndex))
		{
			GraphType::FGraphNode& GraphNode = IslandGraph->GraphNodes[NodeIndex];

			FGeometryParticleHandle* ParticleHandle = GraphNode.NodeItem;
			IslandGraph->UpdateNode(ParticleHandle, IsDynamicParticle(ParticleHandle), GraphNode.IslandIndex,
													IsStationaryParticle(ParticleHandle), NodeIndex);

			if (ParticleHandle->CastToRigidParticle() && ParticleHandle->CastToRigidParticle()->Disabled())
			{
				RemoveParticle(ParticleHandle);
			}
		}
	}
	// For now we are resetting all the constraints but we should keep the
	// persistent collisions, joints...over time
	IslandGraph->InitIslands();
	for(auto& IslandSolver : IslandSolvers)
	{
		if(!IslandSolver->IsSleeping())
		{
			IslandSolver->ClearConstraints();
		}
	}
}

void FPBDIslandManager::RemoveConstraints()
{
	for(auto& ItemEdge : IslandGraph->ItemEdges)
	{
		ItemEdge.Key->SetConstraintGraphIndex(INDEX_NONE);
	}
	IslandGraph->ItemEdges.Reset();
	IslandGraph->GraphEdges.Reset();
	
	for(auto& GraphIsland : IslandGraph->GraphIslands)
	{
		GraphIsland.NumEdges = 0;
	}
	for(auto& GraphNode: IslandGraph->GraphNodes)
	{
		GraphNode.NodeEdges.Reset();
	}
	for(auto& IslandSolver : IslandSolvers)
	{
		IslandSolver->ClearConstraints();
	}
	for(auto& IslandGroup : IslandGroups)
	{
		IslandGroup->NumConstraints() = 0;
	}
}

int32 FPBDIslandManager::ReserveParticles(const int32 NumParticles)
{
	const int32 MaxIndex = IslandGraph->NumNodes();
	IslandGraph->ReserveNodes(NumParticles);
	
	IslandSolvers.Reserve(NumParticles);
	IslandIndexing.Reserve(NumParticles);
	
	return FMath::Max(0,NumParticles - MaxIndex);
}

void FPBDIslandManager::ReserveConstraints(const int32 NumConstraints)
{
	IslandGraph->ReserveEdges(NumConstraints);
}

int32 FPBDIslandManager::AddParticle(FGeometryParticleHandle* ParticleHandle, const int32 IslandIndex, const bool bOnlyDynamic)
{
	if (ParticleHandle)
	{
		const bool bIsDynamic = IsDynamicParticle(ParticleHandle);
		if( !bOnlyDynamic || (bOnlyDynamic && bIsDynamic) )
		{
			MaxParticleIndex = FMath::Max(MaxParticleIndex, ParticleHandle->UniqueIdx().Idx);
			if(FPBDRigidParticleHandle* PBDRigid = ParticleHandle->CastToRigidParticle())
			{
				// If the rigid already have a graph index we just update the node information based on the new particles state...
				if(PBDRigid->ConstraintGraphIndex() != INDEX_NONE)
				{
					IslandGraph->UpdateNode(ParticleHandle, bIsDynamic, IslandIndex,
						IsStationaryParticle(ParticleHandle), PBDRigid->ConstraintGraphIndex());
					return PBDRigid->ConstraintGraphIndex();
				}
			}
			// It could be nice to have a graph index on the particle handle the same way
			// we have one on the constraint handle. It will allow us to skip the query on the TSet to
			// check if the particle is already there, which could be quite slow
			const int32 NodeIndex = IslandGraph->AddNode(ParticleHandle, bIsDynamic, IslandIndex,
				IsStationaryParticle(ParticleHandle));

			if(FPBDRigidParticleHandle* PBDRigid = ParticleHandle->CastToRigidParticle())
			{
				PBDRigid->SetConstraintGraphIndex(NodeIndex);
			}
			return NodeIndex;
		}
	}
	return INDEX_NONE;
}
	
int32 FPBDIslandManager::AddConstraint(const uint32 ContainerId, FConstraintHandle* ConstraintHandle, const TVec2<FGeometryParticleHandle*>& ConstrainedParticles)
{
	if (ConstraintHandle)
	{
		const bool bValidParticle0 = ConstrainedParticles[0] && IsDynamicParticle(ConstrainedParticles[0]);
		const bool bValidParticle1 = ConstrainedParticles[1] && IsDynamicParticle(ConstrainedParticles[1]);
		
		// We are checking if one of the 2 particle is dynamic to add the constraint to the graph
		// Will discard constraints in between 2 sleeping particles. Huge gain but potential side effect?
		if(bValidParticle0 || bValidParticle1)
		{
			const int32 NodeIndex0 = AddParticle(ConstrainedParticles[0], INDEX_NONE, false);
			const int32 NodeIndex1 = AddParticle(ConstrainedParticles[1], INDEX_NONE, false);

			const int32 EdgeIndex = IslandGraph->AddEdge(ConstraintHandle, ContainerId, NodeIndex0, NodeIndex1);
			ConstraintHandle->SetConstraintGraphIndex(EdgeIndex);

			if(IslandGraph->GraphEdges.IsValidIndex(EdgeIndex))
			{
				// Make sure to sync the state of the constraint with its owning island, otherwise the constraint may be flag as destroyable and leave a dangling pointer in the islands
				const int32 IslandIndex = IslandGraph->GraphEdges[EdgeIndex].IslandIndex;
				if (IslandGraph->GraphIslands.IsValidIndex(IslandIndex) && IslandGraph->GraphIslands[IslandIndex].bIsSleeping)
				{
					ConstraintHandle->SetIsSleeping(true);
				}
			}

			return EdgeIndex;
		}
		else
		{
			ConstraintHandle->SetConstraintGraphIndex(INDEX_NONE);
		}
		return INDEX_NONE;
	}
	return INDEX_NONE;
}

void FPBDIslandManager::RemoveParticle(FGeometryParticleHandle* ParticleHandle)
{
	if (ParticleHandle)
	{
		// @todo(chaos) : this is sub-optimal fix for a crash where trying to remove a particle from the data in the graph is not enough 
		// edgelist is empty and particle is in multiple island solver 
		// could possibly be because of the order we delete constraint before removing the particle
		/*for (auto& IslandSolver : IslandSolvers)
		{
			IslandSolver->RemoveParticle(ParticleHandle);
		}*/

		// @todo(chaos) : test that new version to see if we still need the version above 
		if (const int32* NodeIndex = IslandGraph->ItemNodes.Find(ParticleHandle))
		{
			if (IslandGraph->GraphNodes.IsValidIndex(*NodeIndex))
			{
				const FGraphNode& GraphNode = IslandGraph->GraphNodes[*NodeIndex];

				// We loop over all the connected edges to find all the islands in which the particle
				// is (static/kinematic particles could belong to several islands)
				// And remove the particle from the solver island. It will allow the solver
				// islands to be updated directly and not at the next sync.
				for (const int32& EdgeIndex : GraphNode.NodeEdges)
				{
					const int32 IslandIndex = IslandGraph->GraphEdges[EdgeIndex].IslandIndex;
					if (IslandSolvers.IsValidIndex(IslandIndex))
					{
						//We need to remove as well the constraint from the island solver since the edges are removed from the graph
						IslandSolvers[IslandIndex]->RemoveConstraint(IslandGraph->GraphEdges[EdgeIndex].EdgeItem);
						IslandGraph->GraphEdges[EdgeIndex].EdgeItem->SetConstraintGraphIndex(INDEX_NONE);
					}
				}

				// If the node is valid : we need to also remove it from its own island 
				for (const int32& IslandIndex : GraphNode.NodeIslands)
				{
					if (IslandSolvers.IsValidIndex(IslandIndex))
					{
						IslandSolvers[IslandIndex]->RemoveParticle(ParticleHandle);
					}
				}
			}
		}

		IslandGraph->RemoveNode(ParticleHandle);
		if(FPBDRigidParticleHandle* PBDRigid = ParticleHandle->CastToRigidParticle())
		{
			PBDRigid->SetConstraintGraphIndex(INDEX_NONE);
		}
	}
}

void FPBDIslandManager::RemoveConstraint(const uint32 ContainerId, FConstraintHandle* ConstraintHandle)
{
	if (ConstraintHandle)
	{
		const int32 EdgeIndex = ConstraintHandle->ConstraintGraphIndex();
		if (IslandGraph->GraphEdges.IsValidIndex(EdgeIndex))
		{
			const int32 IslandIndex = IslandGraph->GraphEdges[EdgeIndex].IslandIndex;
			if (IslandSolvers.IsValidIndex(IslandIndex))
			{
				IslandSolvers[IslandIndex]->RemoveConstraint(ConstraintHandle);
			}

			IslandGraph->RemoveEdge(EdgeIndex);
			ConstraintHandle->SetConstraintGraphIndex(INDEX_NONE);
		}
	}
}
	
void FPBDIslandManager::EnableParticle(FGeometryParticleHandle* ChildParticle, const FGeometryParticleHandle* ParentParticle)
{
	if (ParentParticle && ChildParticle)
	{
		// We are only adding the child particle to the graph if the parent is dynamic
		const FPBDRigidParticleHandle* ParentPBDRigid = ParentParticle->CastToRigidParticle();
		if (ParentPBDRigid && ParentPBDRigid->ObjectState() == EObjectStateType::Dynamic)
		{
			int32 IslandIndex = INDEX_NONE;
			FPBDRigidParticleHandle* ChildPBDRigid = ChildParticle->CastToRigidParticle();
			if (ChildPBDRigid && IsDynamicParticle(ChildPBDRigid))
			{
				// If the child particle is dynamic or sleeping we are transferring
				// the sleeping flag from the parent to the child
				// and using the parent island index. if the island index we directly
				// update the solver island without waiting the next sync
				IslandIndex = ParentPBDRigid->IslandIndex();
				ChildPBDRigid->SetSleeping(ParentPBDRigid->Sleeping());	
				if (IslandSolvers.IsValidIndex(IslandIndex))
				{
					IslandSolvers[IslandIndex]->AddParticle(ChildParticle);
				}
			}
			// We add the child particle to the graph 
			AddParticle(ChildParticle, IslandIndex);
		}
	}
}

void FPBDIslandManager::DisableParticle(FGeometryParticleHandle* ParticleHandle)
{
	if (ParticleHandle)
	{
		// We remove the particle handle from the graph
		RemoveParticle(ParticleHandle);
	}
}

void FPBDIslandManager::ResetIslands(const TParticleView<FPBDRigidParticles>& PBDRigids)
{
	// @todo 
}

inline bool SolverIslandSortPredicate(const TUniquePtr<FPBDIslandSolver>& SolverIslandL, const TUniquePtr<FPBDIslandSolver>& SolverIslandR)
{
	return SolverIslandL->NumConstraints() < SolverIslandR->NumConstraints();
}
	
void FPBDIslandManager::SyncIslands(FPBDRigidsSOAs& Particles, const int32 NumContainers)
{
	IslandSolvers.Reserve(IslandGraph->NumIslands());
	IslandIndexing.SetNum(IslandGraph->NumIslands(),false);
	SortedIslands.SetNum(IslandGraph->NumIslands(),false);
	int32 LocalIsland = 0;

	// Sync of the solver islands first and reserve the required space
	for (int32 IslandIndex = 0, NumIslands = IslandGraph->NumIslands(); IslandIndex < NumIslands; ++IslandIndex)
	{
		if (IslandGraph->GraphIslands.IsValidIndex(IslandIndex))
		{
			// If the island item is not set on the graph we check if the solver island
			// at the right index is already there. If not we create a new solver island.
			if(!IslandSolvers.IsValidIndex(IslandIndex))
			{
				IslandSolvers.EmplaceAt(IslandIndex, MakeUnique<FPBDIslandSolver>(this, LocalIsland));
			}
			FPBDIslandSolver*& IslandSolver = IslandGraph->GraphIslands[IslandIndex].IslandItem;
			IslandSolver = IslandSolvers[IslandIndex].Get();
			IslandSolver->ResizeConstraintsCounts(NumContainers);
			
			// We then transfer the persistent flag and the graph dense index to the solver island
			IslandSolver->SetIsPersistent(IslandGraph->GraphIslands[IslandIndex].bIsPersistent);
			IslandSolver->SetIsSleeping(IslandGraph->GraphIslands[IslandIndex].bIsSleeping);
			IslandSolver->GetIslandIndex() = LocalIsland;

			// We update the IslandIndexing to retrieve the graph sparse and persistent index from the dense one.
			IslandIndexing[LocalIsland] = IslandIndex;
			SortedIslands[LocalIsland] = IslandIndex;
			LocalIsland++;

			// We finally update the solver islands based on the new particles and constraints if
			// the island is not persistent or not sleeping. 
			if(!IslandSolver->IsSleeping())
			{
				IslandSolver->ReserveParticles(IslandGraph->GraphIslands[IslandIndex].NumNodes);
				IslandSolver->ReserveConstraints(IslandGraph->GraphIslands[IslandIndex].NumEdges);
			}
			// Reset of the sleep counter if the island is :
			// - Non persistent since we are starting incrementing 
			// the counter once the island is persistent and if values below the threshold
			// - Sleeping since as soon as it wakes up we could start incrementing the counter as well
			if (!IslandSolver->IsPersistent() || IslandSolver->IsSleeping())
			{ 
				IslandSolver->SetSleepCounter(0);
			}
			// We reset the persistent flag to be true on the island graph. 
			IslandGraph->GraphIslands[IslandIndex].bIsPersistent = true;
		}
		else if (IslandSolvers.IsValidIndex(IslandIndex))
		{
			IslandSolvers.RemoveAt(IslandIndex);
		}
	}
	PopulateIslands(IslandGraph.Get());

	
	// Update of the sync and sleep state for each islands
	for(auto& IslandSolver : IslandSolvers)
	{
		if(!IslandSolver->IsSleeping() || IslandSolver->SleepingChanged())
		{
			UpdateSyncState(IslandSolver.Get(), Particles);
			UpdateSleepState(IslandSolver.Get(), Particles);
		}
	}
	
	IslandIndexing.SetNum(LocalIsland,false);
	SortedIslands.SetNum(LocalIsland,false);

	// Build all the island groups
	BuildGroups(NumContainers);
}

void FPBDIslandManager::BuildGroups(const int32 NumContainers)
{
	// No Need to sort for now since it is adding a cost for not improving much the end result
	TSparseArray<TUniquePtr<FPBDIslandSolver>>& LocalIslands = IslandSolvers;
	SortedIslands.Sort([&LocalIslands](const int32 IslandIndexA, const int32 IslandIndexB) -> bool
		{ return LocalIslands[IslandIndexA]->NumConstraints() > LocalIslands[IslandIndexB]->NumConstraints();});
	
	const int32 NumGroups = IslandGroups.Num();
	const int32 GroupSize = IslandGraph->NumEdges() / NumGroups + 1;

	for(TUniquePtr<FPBDIslandGroup>& IslandGroup : IslandGroups)
	{
		IslandGroup->InitGroup();
		IslandGroup->ResizeConstraintsCounts(NumContainers);
	}

	int32 GroupIndex = 0;
	int32 GroupOffset = 0;
	for(int32& SortedIndex : SortedIslands)
	{
		if( FPBDIslandSolver* IslandSolver = IslandSolvers[SortedIndex].Get())
		{
			checkf(GroupIndex < NumGroups, TEXT("Index %d is less than NumGroups %d"), GroupIndex, NumGroups);
			IslandGroups[GroupIndex]->AddIsland(IslandSolver);
			IslandGroups[GroupIndex]->NumParticles() += IslandSolver->NumParticles();
			IslandGroups[GroupIndex]->NumConstraints() += IslandSolver->NumConstraints();

			check(IslandSolver->NumContainerIds() == IslandGroups[GroupIndex]->NumContainerIds());
			for(int32 ContainerIndex = 0; ContainerIndex < IslandSolver->NumContainerIds(); ++ContainerIndex)
			{
				IslandGroups[GroupIndex]->ConstraintCount(ContainerIndex) +=
					IslandSolver->ConstraintCount(ContainerIndex);
			}
			
			IslandSolver->SetGroupIndex(GroupIndex);
			GroupOffset += IslandSolver->NumConstraints();

			if(GroupOffset > GroupSize)
			{
				GroupIndex++;
				GroupOffset = 0;
			}
		}
	}
}
	
void FPBDIslandManager::UpdateIslands(const TParticleView<FPBDRigidParticles>& PBDRigids, FPBDRigidsSOAs& Particles, const int32 NumContainers)
{
	// Merge the graph islands if required
	IslandGraph->UpdateGraph();
	
	// Sync the graph islands with the solver islands objects
	SyncIslands(Particles, NumContainers);
}

bool FPBDIslandManager::SleepInactive(const int32 IslandIndex,
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterialAttributes,
	const THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials)
{
	// Only the persistent islands could start sleeping
	const int32 GraphIndex = GetGraphIndex(IslandIndex);
	if (!ChaosSolverSleepEnabled || !IslandSolvers.IsValidIndex(GraphIndex) ||
		(IslandSolvers.IsValidIndex(GraphIndex) && !IslandSolvers[GraphIndex]->IsPersistent()) )
	{
		return false;
	}

	FReal LinearSleepingThreshold = FLT_MAX,  AngularSleepingThreshold = FLT_MAX, MaxLinearSpeed2 = 0.f, MaxAngularSpeed2 = 0.f;
	int32 SleepCounterThreshold = 0, NumDynamicParticles = 0;

	// Compute of the linear/angular velocities + thresholds to make islands sleeping 
	if (ComputeSleepingThresholds(IslandSolvers[GraphIndex].Get(), PerParticleMaterialAttributes,
		SolverPhysicsMaterials, LinearSleepingThreshold, AngularSleepingThreshold,
		MaxLinearSpeed2, MaxAngularSpeed2, SleepCounterThreshold))
	{
		int32 SleepCounter = IslandSolvers[GraphIndex]->GetSleepCounter();
		const bool bSleepingIsland = IsIslandSleeping(MaxLinearSpeed2, MaxAngularSpeed2, LinearSleepingThreshold,
			AngularSleepingThreshold, SleepCounterThreshold, SleepCounter);
		
		IslandSolvers[GraphIndex]->SetSleepCounter(SleepCounter);

		return bSleepingIsland;
	}
	return false;
}

void FPBDIslandManager::SleepIsland(FPBDRigidsSOAs& Particles, const int32 IslandIndex)
{
	const int32 GraphIndex = GetGraphIndex(IslandIndex);
	if (IslandSolvers.IsValidIndex(GraphIndex) && !IslandSolvers[GraphIndex]->IsSleeping())
	{
		IslandSolvers[GraphIndex]->SetIsSleeping(true);
		IslandGraph->GraphIslands[GraphIndex].bIsSleeping = true;
		UpdateSleepState(IslandSolvers[GraphIndex].Get(),Particles);
	}
}

void FPBDIslandManager::WakeIsland(FPBDRigidsSOAs& Particles, const int32 IslandIndex)
{
	const int32 GraphIndex = GetGraphIndex(IslandIndex);
	if (IslandSolvers.IsValidIndex(GraphIndex))
	{
		IslandSolvers[GraphIndex]->SetIsSleeping(false);
		IslandGraph->GraphIslands[GraphIndex].bIsSleeping = false;
		UpdateSleepState(IslandSolvers[GraphIndex].Get(),Particles);
		
		IslandSolvers[GraphIndex]->SetSleepCounter(0);
	}
}
	
const TArray<FGeometryParticleHandle*>& FPBDIslandManager::GetIslandParticles(const int32 IslandIndex) const
{
	return IslandSolvers[GetGraphIndex(IslandIndex)]->GetParticles();
}

const TArray<FConstraintHandleHolder>& FPBDIslandManager::GetIslandConstraints(const int32 IslandIndex) const
{
	return IslandSolvers[GetGraphIndex(IslandIndex)]->GetConstraints();
}

bool FPBDIslandManager::IslandNeedsResim(const int32 IslandIndex) const
{
	return IslandSolvers[GetGraphIndex(IslandIndex)]->NeedsResim();
}

}