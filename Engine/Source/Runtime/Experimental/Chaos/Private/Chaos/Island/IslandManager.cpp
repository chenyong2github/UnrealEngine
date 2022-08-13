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

//PRAGMA_DISABLE_OPTIMIZATION

/** Cvar to enable/disable the island sleeping */
bool bChaosSolverSleepEnabled = true;
FAutoConsoleVariableRef CVarChaosSolverSleepEnabled(TEXT("p.Chaos.Solver.SleepEnabled"), bChaosSolverSleepEnabled, TEXT(""));

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

bool bChaosSolverValidateGraph = (CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED != 0);
FAutoConsoleVariableRef CVarChaosSolverValidateGraph(TEXT("p.Chaos.Solver.ValidateGraph"), bChaosSolverValidateGraph, TEXT(""));

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
		// @todo(chaos): should this also be checking Kinematic W()?
		return KinematicParticle->V().IsZero() && KinematicParticle->KinematicTarget().GetMode() == EKinematicTargetMode::None;
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
			if (FConstraintHandle* ConstraintHandle = IslandConstraint.Get())
			{
				ConstraintHandle->SetIsSleeping(bIsSleeping);
			}
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
		if (IslandGraph->GraphIslands.IsValidIndex(IslandIndex))
		{
			FPBDIslandSolver* IslandSolver = IslandGraph->GraphIslands[IslandIndex].IslandItem;
			if (!IslandSolver->IsSleeping() || IslandSolver->SleepingChanged())
			{
				IslandSolver->AddParticle(GraphNode.NodeItem);
			}
		}
	};
	for (auto& GraphNode : IslandGraph->GraphNodes)
	{
		GraphNode.NodeIslands.Reset();

		// If the node is valid : only one island
		if (GraphNode.bValidNode)
		{
			AddNodeToIsland(GraphNode.IslandIndex, GraphNode);
			GraphNode.NodeIslands.Add(GraphNode.IslandIndex);
		}
		else
		{
			// A particle could belong to several islands when static/kinematic (not valid)
			// First we compute the unique particle set of islands
			for (auto& NodeEdge : GraphNode.NodeEdges)
			{
				GraphNode.NodeIslands.Add(IslandGraph->GraphEdges[NodeEdge].IslandIndex);
			}
			// Loop over the set of islands and add the particle to the solver island
			for (auto& NodeIsland : GraphNode.NodeIslands)
			{
				AddNodeToIsland(NodeIsland, GraphNode);
			}
		}
	}
	
	// Loop over the graph edges to transfer them into the solver island (if awake and involves a dynamic particle)
	// At this point, no awake island should contain any invalid nodes or edges
	for (auto& GraphEdge : IslandGraph->GraphEdges)
	{
		if (IslandGraph->GraphIslands.IsValidIndex(GraphEdge.IslandIndex))
		{
			FPBDIslandSolver* IslandSolver = IslandGraph->GraphIslands[GraphEdge.IslandIndex].IslandItem;

			// Note: we also populate the island if the sleeping state just changed. This is because we use this
			// data to sync the constraint's sleeping state in SyncIslands.
			// @todo(chaos): This is not great - we need a more robust way to sync the sleep state of islands/constraints
			if (!IslandSolver->IsSleeping() || IslandSolver->SleepingChanged())
			{
#if CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED
				ensure(GraphEdge.bValidEdge);
#endif
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

FPBDIslandManager::FPBDIslandManager() 
	: IslandSolvers()
	, IslandGraph(MakeUnique<GraphType>())
	, MaxParticleIndex(INDEX_NONE)
	, bIslandsPopulated(false)
	, bEndTickCalled(true)
{
	IslandGraph->SetOwner(this);

	InitializeGroups();
}

FPBDIslandManager::FPBDIslandManager(const TParticleView<FPBDRigidParticles>& PBDRigids) 
	: IslandSolvers()
	, IslandGraph(MakeUnique<GraphType>())
	, MaxParticleIndex(INDEX_NONE)
	, bIslandsPopulated(false)
	, bEndTickCalled(true)
{
	InitializeGraph(PBDRigids);
	InitializeGroups();
}

FPBDIslandManager::~FPBDIslandManager()
{
}

void FPBDIslandManager::Reset()
{
	RemoveConstraints();
	RemoveParticles();

	IslandSolvers.Reset();

	for (auto& IslandGroup : IslandGroups)
	{
		IslandGroup->NumConstraints() = 0;
	}
}

void FPBDIslandManager::InitializeGroups()
{
	// @todo(chaos): is the number of worker threads a good indicator of how many threads we get in the solver loop? (Currently uses ParallelFor)
	// Check for use of the "-onethread" command line arg, and physics threading disabled (GetNumWorkerThreads() is not affected by these)
	const int32 NumWorkerThreads = (FApp::ShouldUseThreadingForPerformance() && !GSingleThreadedPhysics) ? FMath::Min(FTaskGraphInterface::Get().GetNumWorkerThreads(), Chaos::MaxNumWorkers) : 0;
	const int32 MaxIslandGroups = FMath::Max(1, FMath::CeilToInt32(FReal(NumWorkerThreads) * GChaosSolverIslandGroupsMultiplier));

	IslandGroups.SetNum(MaxIslandGroups, false);
	
	for(int32 GroupIndex = 0, NumGroups = IslandGroups.Num(); GroupIndex < NumGroups; ++GroupIndex)
	{
		IslandGroups[GroupIndex] = MakeUnique<FPBDIslandGroup>(GroupIndex);
	}
}

void FPBDIslandManager::InitializeGraph(const TParticleView<FPBDRigidParticles>& PBDRigids)
{
	// If we hit this, there's a missing call to EndTick since the previous tick
	ensure(bEndTickCalled);
	bEndTickCalled = false;

	// @todo(chaos): make this handle when the user sleeps all particles in an island. Currently this
	// will put the island to sleep as expected, but only after removing all the constraints from the island.
	// The result is that the particles in the island will have no collisions on the first frame after waking.
	// See unit test: DISABLED_TestConstraintGraph_ParticleSleep_Manual

	MaxParticleIndex = 0;
	ReserveParticles(PBDRigids.Num());

	// Reset the island merge tracking data - adding particles may cause islands to merge (e.g., if a particle has switched from kinematic to dynamic
	// @todo(chaos): put this into a method on the graph. Or maybe this isn't necessary since Merge/Split should always leave a valid empty state...
	for (auto& GraphIsland : IslandGraph->GraphIslands)
	{
		GraphIsland.ChildrenIslands.Reset();
		GraphIsland.ParentIsland = INDEX_NONE;
	}

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

			// @todo(chaos): we should not be doing this here - we should remove particles when they are disabled...is this a leftover temp fix?
			if (ParticleHandle->CastToRigidParticle() && ParticleHandle->CastToRigidParticle()->Disabled())
			{
				RemoveParticle(ParticleHandle);
			}
		}
	}

	// For now we are removing all the constraints but really we should keep the persistent constraints and only remove the transients.
	// NOTE: This only removes constraints from awake islands and it leaves the sleeping ones. This is important for 
	// persistent collisions because we do not run collision detection on sleeping particles, so we need the graph
	// to keep it's sleeping edges so we know what collisions to wake when an island is awakened. The constraint
	// management system also uses the sleeping state as a lock to prevent constraint destruction.
	IslandGraph->InitIslands();
}

void FPBDIslandManager::EndTick()
{
	// IslandSolvers are transient for use during the solver phase - clear them at the end of the frame
	for (auto& IslandSolver : IslandSolvers)
	{
		IslandSolver->ClearParticles();
		IslandSolver->ClearConstraints();
		IslandSolver->ResetSleepingChanged();
	}

	// We can no longer use the Island's particle and constraint lists
	bIslandsPopulated = false;

	bEndTickCalled = true;
}

void FPBDIslandManager::RemoveParticles()
{
	RemoveConstraints();

	IslandGraph->ResetNodes();

	for (auto& IslandSolver : IslandSolvers)
	{
		IslandSolver->ClearParticles();
	}
	for (auto& IslandGroup : IslandGroups)
	{
		IslandGroup->NumParticles() = 0;
	}
}

void FPBDIslandManager::RemoveConstraints()
{
	IslandGraph->ResetEdges();

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
	const int32 MaxIndex = IslandGraph->MaxNumNodes();
	IslandGraph->ReserveNodes(NumParticles);
	
	IslandSolvers.Reserve(NumParticles);
	IslandIndexing.Reserve(NumParticles);
	
	return FMath::Max(0,NumParticles - MaxIndex);
}

void FPBDIslandManager::ReserveConstraints(const int32 NumConstraints)
{
	IslandGraph->ReserveEdges(NumConstraints);
}

// Callback from the IslandGraph to allow us to store the node index
void FPBDIslandManager::GraphNodeAdded(FGeometryParticleHandle* ParticleHandle, const int32 NodeIndex)
{
	// @todo(chaos): we should store the node index on kinematics and statics too
	if (FPBDRigidParticleHandle* PBDRigid = ParticleHandle->CastToRigidParticle())
	{
		PBDRigid->SetConstraintGraphIndex(NodeIndex);
	}
}

void FPBDIslandManager::GraphNodeRemoved(FGeometryParticleHandle* ParticleHandle)
{
	if (FPBDRigidParticleHandle* PBDRigid = ParticleHandle->CastToRigidParticle())
	{
		PBDRigid->SetConstraintGraphIndex(INDEX_NONE);
	}
}

// Callback from the IslandGraph to allow us to store the edge index
void FPBDIslandManager::GraphEdgeAdded(const FConstraintHandleHolder& ConstraintHandle, const int32 EdgeIndex)
{
	ConstraintHandle->SetConstraintGraphIndex(EdgeIndex);
}

void FPBDIslandManager::GraphEdgeRemoved(const FConstraintHandleHolder& ConstraintHandle)
{
	ConstraintHandle->SetConstraintGraphIndex(INDEX_NONE);
}

int32 FPBDIslandManager::AddParticle(FGeometryParticleHandle* ParticleHandle, const int32 IslandIndex, const bool bOnlyDynamic)
{
	if (ParticleHandle)
	{
		const bool bIsDynamic = IsDynamicParticle(ParticleHandle);
		if (!bOnlyDynamic || (bOnlyDynamic && bIsDynamic))
		{
			MaxParticleIndex = FMath::Max(MaxParticleIndex, ParticleHandle->UniqueIdx().Idx);

			FPBDRigidParticleHandle* PBDRigid = ParticleHandle->CastToRigidParticle();
			const bool bIsStationary = IsStationaryParticle(ParticleHandle);

			// Assign or update the particle's graph node
			int32 NodeIndex = INDEX_NONE;
			if ((PBDRigid != nullptr) && (PBDRigid->ConstraintGraphIndex() != INDEX_NONE))
			{
				// If the rigid already has a graph index we just update the node information based on the new particles state...
				NodeIndex = PBDRigid->ConstraintGraphIndex();
				IslandGraph->UpdateNode(ParticleHandle, bIsDynamic, IslandIndex, bIsStationary, NodeIndex);
			}
			else
			{
				// If we get here we have a new particle, or a particle that may be in multiple islands (Static/Kinematic)
				NodeIndex = IslandGraph->AddNode(ParticleHandle, bIsDynamic, IslandIndex, bIsStationary);
			}

			return NodeIndex;
		}
	}
	return INDEX_NONE;
}
	
void FPBDIslandManager::AddConstraint(const uint32 ContainerId, FConstraintHandle* ConstraintHandle, const TVec2<FGeometryParticleHandle*>& ConstrainedParticles)
{
	if (ConstraintHandle)
	{
		// Are the particles dynamic (including asleep)?
		const bool bValidParticle0 = ConstrainedParticles[0] && IsDynamicParticle(ConstrainedParticles[0]);
		const bool bValidParticle1 = ConstrainedParticles[1] && IsDynamicParticle(ConstrainedParticles[1]);
		
		// We are checking if one of the 2 particle is dynamic to add the constraint to the graph.
		// NOTE: This is also where kinematic particles get added to the graph since they were not passed to InitializeGraph.
		if(bValidParticle0 || bValidParticle1)
		{
			const int32 NodeIndex0 = AddParticle(ConstrainedParticles[0], INDEX_NONE, false);
			const int32 NodeIndex1 = AddParticle(ConstrainedParticles[1], INDEX_NONE, false);

			const int32 EdgeIndex = IslandGraph->AddEdge(ConstraintHandle, ContainerId, NodeIndex0, NodeIndex1);

			// If we were added to a sleeping island, make sure the constraint is flagged as sleeping.
			// Adding constraints between 2 sleeping particles does not wake them because we need to handle
			// streaming which may amortize particle and constraint creation over multiple ticks.
			const FGraphEdge& GraphEdge = IslandGraph->GraphEdges[EdgeIndex];
			if (GraphEdge.IslandIndex != INDEX_NONE)
			{
				const bool bIsSleeping = IslandGraph->GraphIslands[GraphEdge.IslandIndex].bIsSleeping;
				if (bIsSleeping)
				{
					ConstraintHandle->SetIsSleeping(bIsSleeping);
				}
			}
		}
	}
}

void FPBDIslandManager::RemoveParticle(FGeometryParticleHandle* ParticleHandle)
{
	if (ParticleHandle)
	{
		IslandGraph->RemoveNode(ParticleHandle);

#if CHAOS_CONSTRAINTHANDLE_DEBUG_DETAILED_ENABLED
		DebugCheckParticleNotInGraph(ParticleHandle);
#endif
	}
}

void FPBDIslandManager::RemoveConstraint(const uint32 ContainerId, FConstraintHandle* ConstraintHandle)
{
	if (ConstraintHandle)
	{
		const int32 EdgeIndex = ConstraintHandle->ConstraintGraphIndex();
		if (IslandGraph->GraphEdges.IsValidIndex(EdgeIndex))
		{
			IslandGraph->RemoveEdge(EdgeIndex);
		}

#if CHAOS_CONSTRAINTHANDLE_DEBUG_DETAILED_ENABLED
		DebugCheckConstraintNotInGraph(ConstraintHandle);
#endif
	}
}

void FPBDIslandManager::RemoveParticleConstraints(FGeometryParticleHandle* ParticleHandle, const uint32 ContainerId)
{
	if (ParticleHandle)
	{
		// Find the graph node for this particle
		if (const int32* PNodeIndex = IslandGraph->ItemNodes.Find(ParticleHandle))
		{
			const int32 NodeIndex = *PNodeIndex;
			if (IslandGraph->GraphNodes.IsValidIndex(NodeIndex))
			{
				const FGraphNode& GraphNode = IslandGraph->GraphNodes[NodeIndex];

				// Loop over the edges attached to this node and remove the constraints in the specified container
				// NOTE: Can't use ranged-for iterator when removing items, but we can just loop and remove since it's a sparse array
				for (int32 NodeEdgeIndex = 0, NumNodeEdges = GraphNode.NodeEdges.GetMaxIndex(); NodeEdgeIndex < NumNodeEdges; ++NodeEdgeIndex)
				{
					if (GraphNode.NodeEdges.IsValidIndex(NodeEdgeIndex))
					{
						const int32 EdgeIndex = GraphNode.NodeEdges[NodeEdgeIndex];
						if (IslandGraph->GraphEdges.IsValidIndex(EdgeIndex))
						{
							const FGraphEdge& GraphEdge = IslandGraph->GraphEdges[EdgeIndex];

							// Remove the constraint if it is the right type (same ContainerId)
							if (GraphEdge.ItemContainer == ContainerId)
							{
#if CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED
								// All constraints should know their graph index
								ensure(GraphEdge.EdgeItem->ConstraintGraphIndex() != INDEX_NONE);
#endif

								RemoveConstraint(ContainerId, GraphEdge.EdgeItem.Get());
							}
						}
					}
				}

				// Note: we may have left the particle in the island without any constraints on it. Is that ok?
				// It should be fine as long as the node knows that it is still in it/them, which it does.
			}
		}

#if CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED
		DebugParticleConstraintsNotInGraph(ParticleHandle, ContainerId);
#endif
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
	IslandSolvers.Reserve(IslandGraph->MaxNumIslands());
	IslandIndexing.SetNum(IslandGraph->MaxNumIslands(),false);
	SortedIslands.SetNum(IslandGraph->MaxNumIslands(),false);
	int32 LocalIsland = 0;

	// Sync of the solver islands first and reserve the required space
	for (int32 IslandIndex = 0, NumIslands = IslandGraph->MaxNumIslands(); IslandIndex < NumIslands; ++IslandIndex)
	{
		if (IslandGraph->GraphIslands.IsValidIndex(IslandIndex))
		{
			// NOTE: We do not check !IsSleeping() here because we need to sync the sleep state of islands
			// that were just put to sleep this tick. See PopulateIslands.

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

			// Clear the island solver ready for repopulation . This leaves the sleeping islands empty, but they 
			// are not used until they wake at which point they will be filled again.
			IslandSolver->ReserveParticles(IslandGraph->GraphIslands[IslandIndex].NumNodes);
			IslandSolver->ReserveConstraints(IslandGraph->GraphIslands[IslandIndex].NumEdges);

			// Reset of the sleep counter if the island is :
			// - Non persistent since we are starting incrementing the counter once the island is persistent and if values below the threshold
			// - Sleeping since as soon as it wakes up we could start incrementing the counter as well
			// - Just woken because we may not have been asleep for a whole frame to hit the
			if (!IslandSolver->IsPersistent() || IslandSolver->IsSleeping() || IslandSolver->SleepingChanged())
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

	IslandIndexing.SetNum(LocalIsland, false);
	SortedIslands.SetNum(LocalIsland, false);

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

#if CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED
	DebugCheckIslands();
#endif

	// We can now safely use the GetIslandParticles and GetIslandConstraints accessors
	bIslandsPopulated = true;

	// Build all the island groups
	BuildGroups(NumContainers);

}

void FPBDIslandManager::BuildGroups(const int32 NumContainers)
{
	// No Need to sort for now since it is adding a cost for not improving much the end result
	TSparseArray<TUniquePtr<FPBDIslandSolver>>& LocalIslands = IslandSolvers;
	SortedIslands.Sort([&LocalIslands](const int32 IslandIndexA, const int32 IslandIndexB) -> bool
		{ return LocalIslands[IslandIndexA]->NumConstraints() > LocalIslands[IslandIndexB]->NumConstraints();});

	int32 NumConstraints = 0;
	for (TUniquePtr<FPBDIslandSolver>& IslandSolver : IslandSolvers)
	{
		if (!IslandSolver->IsSleeping())
		{
			NumConstraints += IslandSolver->NumConstraints();
		}
	}
	
	const int32 NumGroups = IslandGroups.Num();
	const int32 GroupSize = NumConstraints / NumGroups + 1;

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
			if (!IslandSolver->IsSleeping())
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

				// @todo: In theory we should never need the second check	
				if(GroupOffset > GroupSize && (GroupIndex + 1) < NumGroups)
				{
					GroupIndex++;
					GroupOffset = 0;
				}
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

	ValidateIslands();
}

void FPBDIslandManager::ValidateIslands() const
{
	if (!bChaosSolverValidateGraph)
	{
		return;
	}

	// Check that nodes joined by an edge are in the same island as the edge
	for (const FGraphEdge& Edge : IslandGraph->GraphEdges)
	{
		const FGraphNode* Node0 = (Edge.FirstNode != INDEX_NONE) ? &IslandGraph->GraphNodes[Edge.FirstNode] : nullptr;
		const FGraphNode* Node1 = (Edge.SecondNode != INDEX_NONE) ? &IslandGraph->GraphNodes[Edge.SecondNode] : nullptr;

		ensure((Node0 == nullptr) || !Node0->bValidNode || (Node0->IslandIndex == Edge.IslandIndex));
		ensure((Node1 == nullptr) || !Node1->bValidNode || (Node1->IslandIndex == Edge.IslandIndex));
	}
}

bool FPBDIslandManager::SleepInactive(const int32 IslandIndex,
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PerParticleMaterialAttributes,
	const THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials)
{
	// Only the persistent islands could start sleeping
	const int32 GraphIndex = GetGraphIndex(IslandIndex);
	if (!bChaosSolverSleepEnabled || !IslandSolvers.IsValidIndex(GraphIndex) ||
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
	// @todo(chaos): this function will not work when called manually at the start of the frame based on user request.
	// Currently this is not required so not important. See disabled unit test: TestConstraintGraph_SleepIsland
	ensure(bIslandsPopulated);

	SetIslandSleeping(Particles, IslandIndex, true);
}

void FPBDIslandManager::SetIslandSleeping(FPBDRigidsSOAs& Particles, const int32 IslandIndex, const bool bIsSleeping)
{
	// NOTE: This Sleep function is called from the graph update to set the state of the 
	// island and all particles in it.
	const int32 GraphIndex = GetGraphIndex(IslandIndex);
	if (IslandSolvers.IsValidIndex(GraphIndex))
	{
		const bool bWasSleeping = IslandSolvers[GraphIndex]->IsSleeping();
		if (bWasSleeping != bIsSleeping)
		{
			IslandSolvers[GraphIndex]->SetIsSleeping(bIsSleeping);
			IslandGraph->GraphIslands[GraphIndex].bIsSleeping = bIsSleeping;
			UpdateSleepState(IslandSolvers[GraphIndex].Get(),Particles);
		}

		// Reset the sleep counter with every wake call, even if already awake
		if (!bIsSleeping)
		{
			IslandSolvers[GraphIndex]->SetSleepCounter(0);
		}
	}
}
	
const TArray<FGeometryParticleHandle*>& FPBDIslandManager::GetIslandParticles(const int32 IslandIndex) const
{
#if CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED
	ensure(bIslandsPopulated);
#endif
	return IslandSolvers[GetGraphIndex(IslandIndex)]->GetParticles();
}

const TArray<FConstraintHandleHolder>& FPBDIslandManager::GetIslandConstraints(const int32 IslandIndex) const
{
#if CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED
	ensure(bIslandsPopulated);
#endif
	return IslandSolvers[GetGraphIndex(IslandIndex)]->GetConstraints();
}

TArray<int32> FPBDIslandManager::FindParticleIslands(const FGeometryParticleHandle* ParticleHandle) const
{
	TArray<int32> IslandIndices;

	// Initially build the list of sparse island indices based on the connected edges (for kinematics)
	if (ParticleHandle)
	{
		// Find the graph node for this particle
		if (const int32* PNodeIndex = IslandGraph->ItemNodes.Find(ParticleHandle))
		{
			const int32 NodeIndex = *PNodeIndex;
			if (IslandGraph->GraphNodes.IsValidIndex(NodeIndex))
			{
				const FGraphNode& GraphNode = IslandGraph->GraphNodes[NodeIndex];

				if (GraphNode.bValidNode)
				{
					// A dynamic particle is only in one island
					if ((GraphNode.IslandIndex != INDEX_NONE) && IslandGraph->GraphIslands.IsValidIndex(GraphNode.IslandIndex))
					{
						IslandIndices.Add(GraphNode.IslandIndex);
					}
				}
				else
				{
					// A non-dynamic particle can be in many islands, depending on the connections to dynamics
					for (int32 EdgeIndex : GraphNode.NodeEdges)
					{
						if (IslandGraph->GraphEdges.IsValidIndex(EdgeIndex))
						{
							const FGraphEdge& GraphEdge = IslandGraph->GraphEdges[EdgeIndex];
							if ((GraphEdge.IslandIndex != INDEX_NONE) && IslandGraph->GraphIslands.IsValidIndex(GraphEdge.IslandIndex))
							{
								IslandIndices.Add(GraphEdge.IslandIndex);
							}
						}
					}
				}
			}
		}

		// Map the islands into non-sparse indices
		for (int32 IslandIndicesIndex = 0; IslandIndicesIndex < IslandIndices.Num(); ++IslandIndicesIndex)
		{
			const int32 GraphIslandIndex = IslandIndices[IslandIndicesIndex];
			const int32 SolverIslandIndex = IslandGraph->GraphIslands[GraphIslandIndex].IslandItem->GetIslandIndex();
			IslandIndices[IslandIndicesIndex] = SolverIslandIndex;
		}
	}

	return IslandIndices;
}

int32 FPBDIslandManager::GetConstraintIsland(const FConstraintHandle* ConstraintHandle) const
{
	const int32 EdgeIndex = ConstraintHandle->ConstraintGraphIndex();
	if (IslandGraph->GraphEdges.IsValidIndex(EdgeIndex))
	{
		int32 GraphIslandIndex = IslandGraph->GraphEdges[EdgeIndex].IslandIndex;
		if (GraphIslandIndex != INDEX_NONE)
		{
			const int32 SolverIslandIndex = IslandGraph->GraphIslands[GraphIslandIndex].IslandItem->GetIslandIndex();
			return SolverIslandIndex;
		}
	}
	return INDEX_NONE;
}

bool FPBDIslandManager::IslandNeedsResim(const int32 IslandIndex) const
{
	return IslandSolvers[GetGraphIndex(IslandIndex)]->NeedsResim();
}

#if CHAOS_CONSTRAINTHANDLE_DEBUG_ENABLED
bool FPBDIslandManager::DebugCheckParticleNotInGraph(const FGeometryParticleHandle* ParticleHandle) const
{
	bool bSuccess = true;
	for (const auto& Node : IslandGraph->GraphNodes)
	{
		if (Node.NodeItem == ParticleHandle)
		{
			bSuccess = false;
			ensure(false);
		}
	}

	for (const auto& ItemNode : IslandGraph->ItemNodes)
	{
		if (ItemNode.Key == ParticleHandle)
		{
			bSuccess = false;
			ensure(false);
		}
	}

	for (const auto& Edge : IslandGraph->GraphEdges)
	{
		if (Edge.EdgeItem.GetParticle0() == ParticleHandle)
		{
			bSuccess = false;
			ensure(false);
		}
		if (Edge.EdgeItem.GetParticle1() == ParticleHandle)
		{
			bSuccess = false;
			ensure(false);
		}
	}
	return bSuccess;
}

bool FPBDIslandManager::DebugParticleConstraintsNotInGraph(const FGeometryParticleHandle* ParticleHandle, const int32 ContainerId) const
{
	bool bSuccess = true;
	for (const auto& Edge : IslandGraph->GraphEdges)
	{
		if (Edge.ItemContainer == ContainerId)
		{
			if ((Edge.EdgeItem.GetParticle0() == ParticleHandle) || (Edge.EdgeItem.GetParticle1() == ParticleHandle))
			{
				bSuccess = false;
				ensure(false);
			}
		}
	}
	return bSuccess;
}

bool FPBDIslandManager::DebugCheckConstraintNotInGraph(const FConstraintHandle* ConstraintHandle) const
{
	bool bSuccess = true;
	for (const auto& Edge : IslandGraph->GraphEdges)
	{
		if (Edge.EdgeItem.Get() == ConstraintHandle)
		{
			bSuccess = false;
			ensure(false);
		}
	}

	for (const auto& ItemEdge : IslandGraph->ItemEdges)
	{
		if (ItemEdge.Key.Get() == ConstraintHandle)
		{
			bSuccess = false;
			ensure(false);
		}
	}

	for (int32 IslandIndex = 0; IslandIndex < NumIslands(); ++IslandIndex)
	{
		const FPBDIslandSolver* IslandSolver = GetSolverIsland(IslandIndex);
		for (const FConstraintHandleHolder& HandleHolder : IslandSolver->GetConstraints())
		{
			if (HandleHolder.Get() == ConstraintHandle)
			{
				bSuccess = false;
				ensure(false);
			}
		}
	}
	return bSuccess;
}

bool FPBDIslandManager::DebugCheckIslandConstraints() const
{
	bool bSuccess = true;

	for (const auto& GraphEdge : IslandGraph->GraphEdges)
	{
		const FConstraintHandleHolder& ConstraintHolder = GraphEdge.EdgeItem;

		// NOTE: The check for both null here is for unit tests that do not use particles...not ideal...
		if ((ConstraintHolder.GetParticle0() != nullptr) || (ConstraintHolder.GetParticle1() != nullptr))
		{
			// Make sure that if we have an IslandIndex it is a valid one
			const bool bIslandIndexOK = (GraphEdge.IslandIndex == INDEX_NONE) || IslandGraph->GraphIslands.IsValidIndex(GraphEdge.IslandIndex);
			bSuccess = bSuccess && bIslandIndexOK;
			ensure(bIslandIndexOK);

			// At least one particle on each constraint must be dynamic if we are in an awake island.
			// If we do not have a dynamic, we must have no island or it must be asleep
			const bool bInAwakeIsland = (GraphEdge.IslandIndex != INDEX_NONE) && IslandGraph->GraphIslands.IsValidIndex(GraphEdge.IslandIndex) && !IslandGraph->GraphIslands[GraphEdge.IslandIndex].bIsSleeping;
			const bool bIsDynamic0 = (ConstraintHolder.GetParticle0() != nullptr) ? FConstGenericParticleHandle(ConstraintHolder.GetParticle0())->IsDynamic() : false;
			const bool bIsDynamic1 = (ConstraintHolder.GetParticle1() != nullptr) ? FConstGenericParticleHandle(ConstraintHolder.GetParticle1())->IsDynamic() : false;
			
			if (bInAwakeIsland)
			{
				// If the island is awake, both particle should be too
				bSuccess = bSuccess && (bIsDynamic0 || bIsDynamic1);
				ensure(bIsDynamic0 || bIsDynamic1);

				// If the island is awake, constraint should be too
				if (ConstraintHolder->SupportsSleeping())
				{
					const bool bIsAsleep = ConstraintHolder->IsSleeping();
					ensure(!bIsAsleep);
				}
			}
			else
			{
				// If the island is asleep, make sure the constraint is too
				if (ConstraintHolder->SupportsSleeping())
				{
					const bool bIsAsleep = ConstraintHolder->IsSleeping();
					ensure(bIsAsleep);
				}
			}

			if (!bIsDynamic0 && !bIsDynamic1)
			{
				bSuccess = bSuccess && !bInAwakeIsland;
				ensure(!bInAwakeIsland);
			}
		}
	}

	return bSuccess;
}

void FPBDIslandManager::DebugCheckParticleIslands(const FGeometryParticleHandle* ParticleHandle) const
{
	// Check that every particle is included in the islands they think and no others
	const int32* PNodeIndex = IslandGraph->ItemNodes.Find(ParticleHandle);
	if (PNodeIndex != nullptr)
	{
		const int32 NodeIndex = *PNodeIndex;
		const auto& Node = IslandGraph->GraphNodes[NodeIndex];
		ensure(Node.NodeItem == ParticleHandle);

		// Find the islands containing this node (either directly, or referenced by an constraint)
		TArray<int32> NodeSolverIslands;
		for (int32 IslandIndex = 0; IslandIndex < NumIslands(); ++IslandIndex)
		{
			const FPBDIslandSolver* IslandSolver = GetSolverIsland(IslandIndex);
			for (const FGeometryParticleHandle* IslandParticleHandle : IslandSolver->GetParticles())
			{
				if (IslandParticleHandle == ParticleHandle)
				{
					NodeSolverIslands.AddUnique(IslandIndex);
				}
			}
			for (const FConstraintHandleHolder& HandleHolder : IslandSolver->GetConstraints())
			{
				if ((HandleHolder.GetParticle0() == ParticleHandle) || (HandleHolder.GetParticle1() == ParticleHandle))
				{
					NodeSolverIslands.AddUnique(IslandIndex);
				}
			}
		}

		// NOTE: Particles are always assigneed to an island, but if we do not have any constraints, the node may not be in the IslandSolver
		if (FConstGenericParticleHandle(ParticleHandle)->IsDynamic())
		{
			// Dynamic particles should be in 1 island
			ensure(Node.IslandIndex != INDEX_NONE);
			ensure(Node.NodeIslands.Num() == 1);
			ensure(Node.NodeIslands.Contains(Node.IslandIndex));
		}
		else
		{
			// Static/Kinematic particles should be in at least 1 island if it has ever been in an awake island...
			// We can't check that explicitly, but if we have no edges we may legitimately have no island
			ensure(Node.IslandIndex == INDEX_NONE);
			ensure((Node.NodeEdges.Num() == 0) || (Node.NodeIslands.Num() >= 1));
		}

		// If we found the node in some solver islands, make sure the nodes know about them
		// NOTE: SolverIsland indexes are into the IslandIndexingArray, not the Islands array
		for (int32 NodeSolverIsland : NodeSolverIslands)
		{
			const int32 NodeIsland = IslandIndexing[NodeSolverIsland];
			ensure(Node.NodeIslands.Contains(NodeIsland));
		}
	}
}

void FPBDIslandManager::DebugCheckConstraintIslands(const FConstraintHandle* ConstraintHandle, const int32 EdgeIslandIndex) const
{
	// Find all the islands containing this edge
	TArray<int32> EdgeSolverIslands;
	for (int32 SolverIslandIndex = 0; SolverIslandIndex < NumIslands(); ++SolverIslandIndex)
	{
		const FPBDIslandSolver* IslandSolver = GetSolverIsland(SolverIslandIndex);
		for (const FConstraintHandleHolder& HandleHolder : IslandSolver->GetConstraints())
		{
			if (HandleHolder.Get() == ConstraintHandle)
			{
				EdgeSolverIslands.AddUnique(SolverIslandIndex);
			}
		}
	}

	if (EdgeIslandIndex != INDEX_NONE)
	{
		if (IslandGraph->GraphIslands[EdgeIslandIndex].bIsSleeping)
		{
			// Make sure the constraint and its island agree on sleepiness
			if (ConstraintHandle->SupportsSleeping())
			{
				ensure(ConstraintHandle->IsSleeping());
			}

			// If we are sleeping, we usually don't appear in the solver islands (because we don't need to solve sleepers)
			// but we do create solver islands for 1 tick after the island goes to sleep so that we can sync the sleep state
			// (see PopulateIslands and SyncSleepState). 
			if (EdgeSolverIslands.Num() > 0)
			{
				ensure(EdgeSolverIslands.Num() == 1);
				const int32 EdgeIsland = IslandIndexing[EdgeSolverIslands[0]];
				ensure(EdgeIslandIndex == EdgeIsland);
			}
		}
		else
		{
			// Make sure the constraint and its island agree on sleepiness
			if (ConstraintHandle->SupportsSleeping())
			{
				ensure(!ConstraintHandle->IsSleeping());
			}

			// We are awake, make sure our island search produced the correct island (and only that one)
			ensure(EdgeSolverIslands.Num() == 1);
			if (EdgeSolverIslands.Num() > 0)
			{
				const int32 EdgeIsland = IslandIndexing[EdgeSolverIslands[0]];
				ensure(EdgeIslandIndex == EdgeIsland);
			}
		}
	}
	else
	{
		// We are not yet added to any islands
		ensure(EdgeSolverIslands.Num() == 0);
	}
}

void FPBDIslandManager::DebugCheckIslands() const
{
	// Check that the solver island state matches the graph island state
	for (int32 IslandIndex = 0; IslandIndex < IslandGraph->GraphIslands.GetMaxIndex(); ++IslandIndex)
	{
		if (IslandGraph->GraphIslands.IsValidIndex(IslandIndex))
		{
			if (IslandGraph->GraphIslands[IslandIndex].bIsSleeping != IslandGraph->GraphIslands[IslandIndex].IslandItem->IsSleeping())
			{
				ensure(false);
			}

			const int32 SolverIslandIndex = IslandGraph->GraphIslands[IslandIndex].IslandItem->GetIslandIndex();
			if (IslandIndexing[SolverIslandIndex] != IslandIndex)
			{
				ensure(false);
			}
		}
	}

	// Make sure islands only contain valid constraints
	DebugCheckIslandConstraints();

	// Check that every particle is included in the islands they think and no others
	for (const auto& Node : IslandGraph->GraphNodes)
	{
		DebugCheckParticleIslands(Node.NodeItem);
	}

	// Check that every constraint is included in the island they think and no others
	for (const auto& Edge : IslandGraph->GraphEdges)
	{
		DebugCheckConstraintIslands(Edge.EdgeItem, Edge.IslandIndex);
	}
}
#endif

}