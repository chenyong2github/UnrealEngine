// Copyright Epic Games, Inc. All Rights Reserved.

#include "PBDRigidsSolver.h"

#include "Async/AsyncWork.h"
#include "Chaos/ChaosArchive.h"
#include "Chaos/PBDCollisionConstraintsUtil.h"
#include "Chaos/Utilities.h"
#include "Chaos/ChaosDebugDraw.h"
#include "ChaosStats.h"
#include "ChaosSolversModule.h"
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsProxy/SkeletalMeshPhysicsProxy.h"
#include "PhysicsProxy/StaticMeshPhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "PhysicsProxy/PerSolverFieldSystem.h"
#include "EventDefaults.h"
#include "EventsData.h"
#include "RewindData.h"
#include "ChaosSolverConfiguration.h"
#include "Chaos/PullPhysicsDataImp.h"
#include "Chaos/PhysicsSolverBaseImpl.h"

//PRAGMA_DISABLE_OPTIMIZATION

DEFINE_LOG_CATEGORY_STATIC(LogPBDRigidsSolver, Log, All);

// DebugDraw CVars
#if CHAOS_DEBUG_DRAW

// Must be 0 when checked in...
#define CHAOS_SOLVER_ENABLE_DEBUG_DRAW 1

int32 ChaosSolverDebugDrawShapes = CHAOS_SOLVER_ENABLE_DEBUG_DRAW;
int32 ChaosSolverDebugDrawCollisions = CHAOS_SOLVER_ENABLE_DEBUG_DRAW;
int32 ChaosSolverDebugDrawBounds = 0;
int32 ChaosSolverDrawTransforms = 0;
int32 ChaosSolverDrawIslands = 0;
FAutoConsoleVariableRef CVarChaosSolverDrawShapes(TEXT("p.Chaos.Solver.DebugDrawShapes"), ChaosSolverDebugDrawShapes, TEXT("Draw Shapes (0 = never; 1 = end of frame)."));
FAutoConsoleVariableRef CVarChaosSolverDrawCollisions(TEXT("p.Chaos.Solver.DebugDrawCollisions"), ChaosSolverDebugDrawCollisions, TEXT("Draw Collisions (0 = never; 1 = end of frame)."));
FAutoConsoleVariableRef CVarChaosSolverDrawBounds(TEXT("p.Chaos.Solver.DebugDrawBounds"), ChaosSolverDebugDrawBounds, TEXT("Draw bounding volumes inside the broadphase (0 = never; 1 = end of frame)."));
FAutoConsoleVariableRef CVarChaosSolverDrawTransforms(TEXT("p.Chaos.Solver.DebugDrawTransforms"), ChaosSolverDrawTransforms, TEXT("Draw particle transforms (0 = never; 1 = end of frame)."));
FAutoConsoleVariableRef CVarChaosSolverDrawIslands(TEXT("p.Chaos.Solver.DebugDrawIslands"), ChaosSolverDrawIslands, TEXT("Draw solver islands (0 = never; 1 = end of frame)."));

Chaos::DebugDraw::FChaosDebugDrawSettings ChaosSolverDebugDebugDrawSettings(
	/* ArrowSize =			*/ 10.0f,
	/* BodyAxisLen =		*/ 30.0f,
	/* ContactLen =			*/ 30.0f,
	/* ContactWidth =		*/ 6.0f,
	/* ContactPhiWidth =	*/ 0.0f,
	/* ContactOwnerWidth =	*/ 0.0f,
	/* ConstraintAxisLen =	*/ 30.0f,
	/* JointComSize =		*/ 2.0f,
	/* LineThickness =		*/ 1.5f,
	/* DrawScale =			*/ 1.0f,
	/* FontHeight =			*/ 10.0f,
	/* FontScale =			*/ 1.5f,
	/* ShapeThicknesScale = */ 1.0f,
	/* PointSize =			*/ 2.0f,
	/* VelScale =			*/ 1.0f,
	/* AngVelScale =		*/ 0.0f,
	/* ImpulseScale =		*/ 0.05f,
	/* DrawPriority =		*/ 10.0f
);
FAutoConsoleVariableRef CVarChaosSolverArrowSize(TEXT("p.Chaos.Solver.DebugDraw.ArrowSize"), ChaosSolverDebugDebugDrawSettings.ArrowSize, TEXT("ArrowSize."));
FAutoConsoleVariableRef CVarChaosSolverBodyAxisLen(TEXT("p.Chaos.Solver.DebugDraw.BodyAxisLen"), ChaosSolverDebugDebugDrawSettings.BodyAxisLen, TEXT("BodyAxisLen."));
FAutoConsoleVariableRef CVarChaosSolverContactLen(TEXT("p.Chaos.Solver.DebugDraw.ContactLen"), ChaosSolverDebugDebugDrawSettings.ContactLen, TEXT("ContactLen."));
FAutoConsoleVariableRef CVarChaosSolverContactWidth(TEXT("p.Chaos.Solver.DebugDraw.ContactWidth"), ChaosSolverDebugDebugDrawSettings.ContactWidth, TEXT("ContactWidth."));
FAutoConsoleVariableRef CVarChaosSolverContactPhiWidth(TEXT("p.Chaos.Solver.DebugDraw.ContactPhiWidth"), ChaosSolverDebugDebugDrawSettings.ContactPhiWidth, TEXT("ContactPhiWidth."));
FAutoConsoleVariableRef CVarChaosSolverContactOwnerWidth(TEXT("p.Chaos.Solver.DebugDraw.ContactOwnerWidth"), ChaosSolverDebugDebugDrawSettings.ContactOwnerWidth, TEXT("ContactOwnerWidth."));
FAutoConsoleVariableRef CVarChaosSolverConstraintAxisLen(TEXT("p.Chaos.Solver.DebugDraw.ConstraintAxisLen"), ChaosSolverDebugDebugDrawSettings.ConstraintAxisLen, TEXT("ConstraintAxisLen."));
FAutoConsoleVariableRef CVarChaosSolverLineThickness(TEXT("p.Chaos.Solver.DebugDraw.LineThickness"), ChaosSolverDebugDebugDrawSettings.LineThickness, TEXT("LineThickness."));
FAutoConsoleVariableRef CVarChaosSolverLineShapeThickness(TEXT("p.Chaos.Solver.DebugDraw.ShapeLineThicknessScale"), ChaosSolverDebugDebugDrawSettings.ShapeThicknesScale, TEXT("Shape lineThickness multiplier."));
FAutoConsoleVariableRef CVarChaosSolverPointSize(TEXT("p.Chaos.Solver.DebugDraw.PointSize"), ChaosSolverDebugDebugDrawSettings.PointSize, TEXT("Point size."));
FAutoConsoleVariableRef CVarChaosSolverVelScale(TEXT("p.Chaos.Solver.DebugDraw.VelScale"), ChaosSolverDebugDebugDrawSettings.VelScale, TEXT("If >0 show velocity when drawing particle transforms."));
FAutoConsoleVariableRef CVarChaosSolverAngVelScale(TEXT("p.Chaos.Solver.DebugDraw.AngVelScale"), ChaosSolverDebugDebugDrawSettings.AngVelScale, TEXT("If >0 show angular velocity when drawing particle transforms."));
FAutoConsoleVariableRef CVarChaosSolverImpulseScale(TEXT("p.Chaos.Solver.DebugDraw.ImpulseScale"), ChaosSolverDebugDebugDrawSettings.ImpulseScale, TEXT("If >0 show impulses when drawing collisions."));
FAutoConsoleVariableRef CVarChaosSolverScale(TEXT("p.Chaos.Solver.DebugDraw.Scale"), ChaosSolverDebugDebugDrawSettings.DrawScale, TEXT("Scale applied to all Chaos Debug Draw line lengths etc."));

#endif

bool ChaosSolverUseParticlePool = true;
FAutoConsoleVariableRef CVarChaosSolverUseParticlePool(TEXT("p.Chaos.Solver.UseParticlePool"), ChaosSolverUseParticlePool, TEXT("Whether or not to use dirty particle pool (Optim)"));

int32 ChaosSolverParticlePoolNumFrameUntilShrink = 30;
FAutoConsoleVariableRef CVarChaosSolverParticlePoolNumFrameUntilShrink(TEXT("p.Chaos.Solver.ParticlePoolNumFrameUntilShrink"), ChaosSolverParticlePoolNumFrameUntilShrink, TEXT("Num Frame until we can potentially shrink the pool"));

// Iteration count cvars
// These override the engine config if >= 0

int32 ChaosSolverIterations = -1;
FAutoConsoleVariableRef CVarChaosSolverIterations(TEXT("p.Chaos.Solver.Iterations"), ChaosSolverIterations, TEXT("Override umber of solver iterations (-1 to use config)"));

int32 ChaosSolverCollisionIterations = -1;
FAutoConsoleVariableRef CVarChaosSolverCollisionIterations(TEXT("p.Chaos.Solver.Collision.Iterations"), ChaosSolverCollisionIterations, TEXT("Override number of collision iterations per solver iteration (-1 to use config)"));

int32 ChaosSolverPushOutIterations = -1;
FAutoConsoleVariableRef CVarChaosSolverPushOutIterations(TEXT("p.Chaos.Solver.PushoutIterations"), ChaosSolverPushOutIterations, TEXT("Override number of solver pushout iterations (-1 to use config)"));

int32 ChaosSolverCollisionPushOutIterations = -1;
FAutoConsoleVariableRef CVarChaosSolverCollisionPushOutIterations(TEXT("p.Chaos.Solver.Collision.PushOutIterations"), ChaosSolverCollisionPushOutIterations, TEXT("Override number of collision iterations per solver iteration (-1 to use config)"));

int32 ChaosSolverJointPairIterations = -1;
FAutoConsoleVariableRef CVarChaosSolverJointPairIterations(TEXT("p.Chaos.Solver.Joint.PairIterations"), ChaosSolverJointPairIterations, TEXT("Override number of iterations per joint pair during a solver iteration (-1 to use config)"));

int32 ChaosSolverJointPushOutPairIterations = -1;
FAutoConsoleVariableRef CVarChaosSolverJointPushOutPairIterations(TEXT("p.Chaos.Solver.Joint.PushOutPairIterations"), ChaosSolverJointPushOutPairIterations, TEXT("Override number of push out iterations per joint during a solver iteration (-1 to use config)"));


// Collision detection cvars
// These override the engine config if >= 0
float ChaosSolverCullDistance = -1.0f;
FAutoConsoleVariableRef CVarChaosSolverCullDistance(TEXT("p.Chaos.Solver.Collision.CullDistance"), ChaosSolverCullDistance, TEXT("Override cull distance (if >= 0)"));

int32 ChaosSolverCleanupCommandsOnDestruction = 1;
FAutoConsoleVariableRef CVarChaosSolverCleanupCommandsOnDestruction(TEXT("p.Chaos.Solver.CleanupCommandsOnDestruction"), ChaosSolverCleanupCommandsOnDestruction, TEXT("Whether or not to run internal command queue cleanup on solver destruction (0 = no cleanup, >0 = cleanup all commands)"));

int32 ChaosSolverCollisionDeferNarrowPhase = 0;
FAutoConsoleVariableRef CVarChaosSolverCollisionDeferNarrowPhase(TEXT("p.Chaos.Solver.Collision.DeferNarrowPhase"), ChaosSolverCollisionDeferNarrowPhase, TEXT("Create contacts for all broadphase pairs, perform NarrowPhase later."));

// Old manifold system
int32 ChaosSolverCollisionUseManifolds = 0;
FAutoConsoleVariableRef CVarChaosSolverCollisionUseManifolds(TEXT("p.Chaos.Solver.Collision.UseManifolds"), ChaosSolverCollisionUseManifolds, TEXT("Enable/Disable use of manifoldes in collision."));

// New manifold system
int32 ChaosSolverCollisionUseIncrememtalManifolds = 1;
FAutoConsoleVariableRef CVarChaosUseIncrementalManifold(TEXT("p.Chaos.Solver.Collision.UseIncrementalManifolds"), ChaosSolverCollisionUseIncrememtalManifolds, TEXT("Enable/Disable use of incremental manifolds."));

// New One-shot manifolds (only for boxes right now)
int32 ChaosSolverCollisionUseOneShotManifolds = 1;
FAutoConsoleVariableRef CVarChaosUseOneShotManifold(TEXT("p.Chaos.Solver.Collision.UseOneShotManifolds"), ChaosSolverCollisionUseOneShotManifolds, TEXT("Enable/Disable use of OneShot manifolds where available. If enabled Incremental manifold setting will be ignored"));

int32 ChaosVisualDebuggerEnable = 1;
FAutoConsoleVariableRef CVarChaosVisualDebuggerEnable(TEXT("p.Chaos.VisualDebuggerEnable"), ChaosVisualDebuggerEnable, TEXT("Enable/Disable pushing/saving data to the visual debugger"));

namespace Chaos
{

	template <typename Traits>
	class AdvanceOneTimeStepTask : public FNonAbandonableTask
	{
		friend class FAutoDeleteAsyncTask<AdvanceOneTimeStepTask>;
	public:
		AdvanceOneTimeStepTask(
			TPBDRigidsSolver<Traits>* Scene
			, const float DeltaTime
			, const FSubStepInfo& SubStepInfo)
			: MSolver(Scene)
			, MDeltaTime(DeltaTime)
			, MSubStepInfo(SubStepInfo)
		{
			UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("AdvanceOneTimeStepTask::AdvanceOneTimeStepTask()"));
		}

		void DoWork()
		{
			LLM_SCOPE(ELLMTag::Chaos);
			UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("AdvanceOneTimeStepTask::DoWork()"));
			MSolver->StartingSceneSimulation();

			if(MDeltaTime > 0)	//if delta time is 0 we are flushing data, user callbacks should not be triggered because there is no sim
			{
				MSolver->ApplyCallbacks_Internal(MSolver->GetSolverTime(), MDeltaTime);	//question: is SolverTime the right thing to pass in here?
			}
			MSolver->GetEvolution()->GetRigidClustering().ResetAllClusterBreakings();

			{
				SCOPE_CYCLE_COUNTER(STAT_UpdateParams);
				Chaos::TPBDPositionConstraints<float, 3> PositionTarget; // Dummy for now
				TMap<int32, int32> PositionTargetedParticles;
				//TArray<FKinematicProxy> AnimatedPositions;
				Chaos::TArrayCollectionArray<float> Strains;
				{
					FPerSolverFieldSystem& FieldObj = MSolver->GetPerSolverField();
					auto& GeomCollectionParticles = MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles();
					FieldObj.FieldParameterUpdateCallback(MSolver, GeomCollectionParticles, Strains,
						PositionTarget, PositionTargetedParticles, /*AnimatedPositions,*/ MSolver->GetSolverTime());
					auto& ClusteredParticles = MSolver->GetEvolution()->GetParticles().GetClusteredParticles();
					FieldObj.FieldParameterUpdateCallback(MSolver, ClusteredParticles, Strains,
						PositionTarget, PositionTargetedParticles, /*AnimatedPositions,*/ MSolver->GetSolverTime());
				}

				for (FGeometryCollectionPhysicsProxy* Obj : MSolver->GetGeometryCollectionPhysicsProxies_Internal())
				{
					Obj->ParameterUpdateCallback(MSolver, MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles(), MSolver->GetSolverTime());
				}

				MSolver->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager().ProcessPendingQueues();
			}

			{
				//SCOPE_CYCLE_COUNTER(STAT_BeginFrame);
				//MSolver->StartFrameCallback(MDeltaTime, MSolver->GetSolverTime());
			}


			if(FRewindData* RewindData = MSolver->GetRewindData())
			{
				RewindData->AdvanceFrame(MDeltaTime,[Evolution = MSolver->GetEvolution()]()
				{
					return Evolution->CreateExternalResimCache();
				});
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EvolutionAndKinematicUpdate);

				// This outer loop can potentially cause the system to lose energy over integration
				// in a couple of different cases.
				//
				// * If we have a timestep that's smaller than MinDeltaTime, then we just won't step.
				//   Yes, we'll lose some teeny amount of energy, but we'll avoid 1/dt issues.
				//
				// * If we have used all of our substeps but still have time remaining, then some
				//   energy will be lost.
				const float MinDeltaTime = MSolver->GetMinDeltaTime();
				const float MaxDeltaTime = MSolver->GetMaxDeltaTime();
				int32 StepsRemaining = MSolver->GetMaxSubSteps();
				float TimeRemaining = MDeltaTime;
				bool bFirstStep = true;
				while (StepsRemaining > 0 && TimeRemaining > MinDeltaTime)
				{
					--StepsRemaining;
					const float DeltaTime = MaxDeltaTime > 0.f ? FMath::Min(TimeRemaining, MaxDeltaTime) : TimeRemaining;
					TimeRemaining -= DeltaTime;

					Chaos::TArrayCollectionArray<FVector> Forces, Torques;
					{
						FPerSolverFieldSystem& FieldObj = MSolver->GetPerSolverField();
						auto& GeomCollectionParticles = MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles();
						FieldObj.FieldForcesUpdateCallback(MSolver, GeomCollectionParticles, Forces, Torques, MSolver->GetSolverTime());
						auto& ClusteredParticles = MSolver->GetEvolution()->GetParticles().GetClusteredParticles();
						FieldObj.FieldForcesUpdateCallback(MSolver, ClusteredParticles, Forces, Torques, MSolver->GetSolverTime());
					}

					for (FGeometryCollectionPhysicsProxy* Obj : MSolver->GetGeometryCollectionPhysicsProxies_Internal())
					{
						Obj->ParameterUpdateCallback(MSolver, MSolver->GetEvolution()->GetParticles().GetGeometryCollectionParticles(), MSolver->GetSolverTime());
					}

					if(FRewindData* RewindData = MSolver->GetRewindData())
					{
						//todo: make this work with sub-stepping
						MSolver->GetEvolution()->SetCurrentStepResimCache(bFirstStep ? RewindData->GetCurrentStepResimCache() : nullptr);
					}

					MSolver->GetEvolution()->AdvanceOneTimeStep(DeltaTime, MSubStepInfo);
					MSolver->PostEvolutionVDBPush();
					bFirstStep = false;
				}

				// Editor will tick with 0 DT, this will guarantee acceleration structure is still processing even if we don't advance evolution.
				if (MDeltaTime < MinDeltaTime)
				{
					MSolver->GetEvolution()->ComputeIntermediateSpatialAcceleration();
				}

#if CHAOS_CHECKED
				// If time remains, then log why we have lost energy over the timestep.
				if (TimeRemaining > 0.f)
				{
					if (StepsRemaining == 0)
					{
						UE_LOG(LogPBDRigidsSolver, Warning, TEXT("AdvanceOneTimeStepTask::DoWork() - Energy lost over %fs due to too many substeps over large timestep"), TimeRemaining);
					}
					else
					{
						UE_LOG(LogPBDRigidsSolver, Warning, TEXT("AdvanceOneTimeStepTask::DoWork() - Energy lost over %fs due to small timestep remainder"), TimeRemaining);
					}
				}
#endif
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EventDataGathering);
				{
					SCOPE_CYCLE_COUNTER(STAT_FillProducerData);
					MSolver->GetEventManager()->FillProducerData(MSolver);
				}
				{
					SCOPE_CYCLE_COUNTER(STAT_FlipBuffersIfRequired);
					MSolver->GetEventManager()->FlipBuffersIfRequired();
				}
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_EndFrame);
				MSolver->GetEvolution()->EndFrame(MDeltaTime);
			}

			if(FRewindData* RewindData = MSolver->GetRewindData())
			{
				RewindData->FinishFrame();
			}

			MSolver->FinalizeCallbackData_Internal();

			MSolver->GetSolverTime() += MDeltaTime;
			MSolver->GetCurrentFrame()++;
			MSolver->PostTickDebugDraw(MDeltaTime);

			//Editor ticks with 0 dt. We don't want to buffer any dirty data from this since it won't be consumed
			//TODO: handle this more gracefully
			if(MDeltaTime > 0)
			{
				MSolver->CompleteSceneSimulation();
			}
		}

	protected:

		TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(AdvanceOneTimeStepTask, STATGROUP_ThreadPoolAsyncTasks);
		}

		TPBDRigidsSolver<Traits>* MSolver;
		float MDeltaTime;
		FSubStepInfo MSubStepInfo;
		TSharedPtr<FCriticalSection> PrevLock, CurrentLock;
		TSharedPtr<FEvent> PrevEvent, CurrentEvent;
	};

	template <typename Traits>
	TPBDRigidsSolver<Traits>::TPBDRigidsSolver(const EMultiBufferMode BufferingModeIn, UObject* InOwner)
		: Super(BufferingModeIn, BufferingModeIn == EMultiBufferMode::Single ? EThreadingModeTemp::SingleThread : EThreadingModeTemp::TaskGraph, InOwner, TraitToIdx<Traits>())
		, CurrentFrame(0)
		, MTime(0.0)
		, MLastDt(0.0)
		, MMaxDeltaTime(0.0)
		, MMinDeltaTime(SMALL_NUMBER)
		, MMaxSubSteps(1)
		, bHasFloor(true)
		, bIsFloorAnalytic(false)
		, FloorHeight(0.f)
		, MEvolution(new FPBDRigidsEvolution(Particles, SimMaterials, &ContactModifiers, BufferingModeIn == Chaos::EMultiBufferMode::Single))
		, MEventManager(new TEventManager<Traits>(BufferingModeIn))
		, MSolverEventFilters(new FSolverEventFilters())
		, MDirtyParticlesBuffer(new FDirtyParticlesBuffer(BufferingModeIn, BufferingModeIn == Chaos::EMultiBufferMode::Single))
		, MCurrentLock(new FCriticalSection())
		, bUseCollisionResimCache(false)
		, JointConstraintRule(JointConstraints)
		, SuspensionConstraintRule(SuspensionConstraints)
		, PerSolverField(nullptr)
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("PBDRigidsSolver::PBDRigidsSolver()"));
		Reset();
		MEvolution->AddConstraintRule(&JointConstraintRule);
		MEvolution->AddConstraintRule(&SuspensionConstraintRule);

		MEvolution->SetInternalParticleInitilizationFunction(
			[this](const Chaos::TGeometryParticleHandle<float, 3>* OldParticle, const Chaos::TGeometryParticleHandle<float, 3>* NewParticle) {
				if (const TSet<IPhysicsProxyBase*>* Proxies = GetProxies(OldParticle))
				{
					for (IPhysicsProxyBase* Proxy : *Proxies)
					{
						this->AddParticleToProxy(NewParticle, Proxy);
					}
				}
			});
	}

	float MaxBoundsForTree = 10000;
	FAutoConsoleVariableRef CVarMaxBoundsForTree(
		TEXT("p.MaxBoundsForTree"),
		MaxBoundsForTree,
		TEXT("The max bounds before moving object into a large objects structure. Only applies on object registration")
		TEXT(""),
		ECVF_Default);

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::RegisterObject(TGeometryParticle<float, 3>* GTParticle)
	{
		LLM_SCOPE(ELLMTag::Chaos);

		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("TPBDRigidsSolver::RegisterObject()"));

		// Make sure this particle doesn't already have a proxy
		checkSlow(GTParticle->GetProxy() == nullptr);

		if (GTParticle->Geometry() && GTParticle->Geometry()->HasBoundingBox() && GTParticle->Geometry()->BoundingBox().Extents().Max() >= MaxBoundsForTree)
		{
			GTParticle->SetSpatialIdx(FSpatialAccelerationIdx{ 1,0 });
		}
		if (!ensure(GTParticle->IsParticleValid()))
		{
			return;
		}

		// NOTE: Do we really need these lists of proxies if we can just
		// access them through the GTParticles list?
		
		IPhysicsProxyBase* ProxyBase;

		GTParticle->SetUniqueIdx(GetEvolution()->GenerateUniqueIdx());
		TrackGTParticle_External(*GTParticle);
		//Chaos::FParticlePropertiesData& RemoteParticleData = *DirtyPropertiesManager->AccessProducerBuffer()->NewRemoteParticleProperties();
		//Chaos::FShapeRemoteDataContainer& RemoteShapeContainer = *DirtyPropertiesManager->AccessProducerBuffer()->NewRemoteShapeContainer();

		// Make a physics proxy, giving it our particle and particle handle
		const EParticleType InParticleType = GTParticle->ObjectType();
		if (InParticleType == EParticleType::Rigid)
		{
			auto Proxy = new FRigidParticlePhysicsProxy(GTParticle->CastToRigidParticle(), nullptr);
			RigidParticlePhysicsProxies.Add((FRigidParticlePhysicsProxy*)Proxy);
			ProxyBase = Proxy;
		}
		else if (InParticleType == EParticleType::Kinematic)
		{
			auto Proxy = new FKinematicGeometryParticlePhysicsProxy(GTParticle->CastToKinematicParticle(), nullptr);
			KinematicGeometryParticlePhysicsProxies.Add((FKinematicGeometryParticlePhysicsProxy*)Proxy);
			ProxyBase = Proxy;
		}
		else // Assume it's a static (geometry) if it's not dynamic or kinematic
		{
			auto Proxy = new FGeometryParticlePhysicsProxy(GTParticle, nullptr);
			GeometryParticlePhysicsProxies.Add((FGeometryParticlePhysicsProxy*)Proxy);
			ProxyBase = Proxy;
		}

		ProxyBase->SetSolver(this);

		// Associate the proxy with the particle
		GTParticle->SetProxy(ProxyBase);

		AddDirtyProxy(ProxyBase);

		UpdateParticleInAccelerationStructure_External(GTParticle, /*bDelete=*/false);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::UnregisterObject(TGeometryParticle<float, 3>* GTParticle)
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("TPBDRigidsSolver::UnregisterObject()"));

		// Get the proxy associated with this particle
		IPhysicsProxyBase* InProxy = GTParticle->GetProxy();
		check(InProxy);

		// Grab the particle's type
		const EParticleType InParticleType = GTParticle->ObjectType();

		ClearGTParticle_External(*GTParticle);

		UpdateParticleInAccelerationStructure_External(GTParticle, /*bDelete=*/true);

		// remove the proxy from the invalidation list
		RemoveDirtyProxy(GTParticle->GetProxy());

		// mark proxy timestamp so we avoid trying to pull from sim after deletion
		GTParticle->GetProxy()->SetSyncTimestamp(MarshallingManager.GetExternalTimestamp_External());

		// Null out the particle's proxy pointer
		GTParticle->SetProxy(nullptr);

		// Remove the proxy from the GT proxy map
		if (InParticleType == EParticleType::Rigid)
		{
			RigidParticlePhysicsProxies.RemoveSingleSwap((FRigidParticlePhysicsProxy*)InProxy);
		}
		else if (InParticleType == EParticleType::Kinematic)
		{
			KinematicGeometryParticlePhysicsProxies.RemoveSingleSwap((FKinematicGeometryParticlePhysicsProxy*)InProxy);
		}
		else if (InParticleType == EParticleType::GeometryCollection)
		{
			check(false); // This shouldn't happen.
		}
		else
		{
			GeometryParticlePhysicsProxies.RemoveSingleSwap((FGeometryParticlePhysicsProxy*)InProxy);
		}

		// Enqueue a command to remove the particle and delete the proxy
		EnqueueCommandImmediate([InProxy, InParticleType, this]()
		{
			UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("TPBDRigidsSolver::UnregisterObject() ~ Dequeue"));

				// Generally need to remove stale events for particles that no longer exist
				GetEventManager()->template ClearEvents<FCollisionEventData>(EEventType::Collision, [InProxy]
				(FCollisionEventData& EventDataInOut)
				{
					Chaos::FCollisionDataArray const& CollisionData = EventDataInOut.CollisionData.AllCollisionsArray;
					if (CollisionData.Num() > 0)
					{
						check(InProxy);
						TArray<int32> const* const CollisionIndices = EventDataInOut.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap.Find(InProxy);
						if (CollisionIndices)
						{
							for (int32 EncodedCollisionIdx : *CollisionIndices)
							{
								bool bSwapOrder;
								int32 CollisionIdx = Chaos::TEventManager<Traits>::DecodeCollisionIndex(EncodedCollisionIdx, bSwapOrder);

								// invalidate but don't delete from array, as this would mean we'd need to reindex PhysicsProxyToIndicesMap to maintain the other collisions lookup
								Chaos::TCollisionData<float, 3>& CollisionDataItem = EventDataInOut.CollisionData.AllCollisionsArray[CollisionIdx];
								CollisionDataItem.ParticleProxy = nullptr;
								CollisionDataItem.LevelsetProxy = nullptr;
							}

							EventDataInOut.PhysicsProxyToCollisionIndices.PhysicsProxyToIndicesMap.Remove(InProxy);
						}
					}

				});

			// Get the physics thread-handle from the proxy, and then delete the proxy.
			//
			// NOTE: We have to delete the proxy from its derived version, because the
			// base destructor is protected. This makes everything just a bit uglier,
			// maybe that extra safety is not needed if we continue to contain all
			// references to proxy instances entirely in Chaos?
			TGeometryParticleHandle<float, 3>* Handle;
			if (InParticleType == Chaos::EParticleType::Rigid)
			{
				auto Proxy = (FRigidParticlePhysicsProxy*)InProxy;
				Handle = Proxy->GetHandle();
				Proxy->SetHandle(nullptr);
				PendingDestroyRigidProxy.Add(Proxy);
			}
			else if (InParticleType == Chaos::EParticleType::Kinematic)
			{
				auto Proxy = (FKinematicGeometryParticlePhysicsProxy*)InProxy;
				Handle = Proxy->GetHandle();
				Proxy->SetHandle(nullptr);
				PendingDestroyKinematicProxy.Add(Proxy);
			}
			else
			{
				auto Proxy = (FGeometryParticlePhysicsProxy*)InProxy;
				Handle = Proxy->GetHandle();
				Proxy->SetHandle(nullptr);
				PendingDestroyGeometryProxy.Add(Proxy);
			}

			//If particle was created and destroyed before commands were enqueued just skip. I suspect we can skip entire lambda, but too much code to verify right now

			if(Handle)
			{
				// Remove from rewind data
				if(FRewindData* RewindData = GetRewindData())
				{
					RewindData->RemoveParticle(Handle->UniqueIdx());
				}
  
				MParticleToProxy.Remove(Handle);
  
				// Use the handle to destroy the particle data
				GetEvolution()->DestroyParticle(Handle);
			}

		});

	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::RegisterObject(FGeometryCollectionPhysicsProxy* InProxy)
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("TPBDRigidsSolver::RegisterObject(FGeometryCollectionPhysicsProxy*)"));
		GeometryCollectionPhysicsProxies_External.AddUnique(InProxy);
		InProxy->SetSolver(this);
		InProxy->Initialize();
		InProxy->NewData(); // Buffers data on the proxy.
		FParticlesType* InParticles = &GetParticles();

		// Finish registration on the physics thread...
		EnqueueCommandImmediate([InParticles, InProxy, this]()
		{
			UE_LOG(LogPBDRigidsSolver, Verbose, 
				TEXT("TPBDRigidsSolver::RegisterObject(FGeometryCollectionPhysicsProxy*)"));
			check(InParticles);
			InProxy->InitializeBodiesPT(this, *InParticles);
			GeometryCollectionPhysicsProxies_Internal.Add(InProxy);
		});
	}
	
	template <typename Traits>
	bool TPBDRigidsSolver<Traits>::UnregisterObject(FGeometryCollectionPhysicsProxy* InProxy)
	{
		check(InProxy);
		// mark proxy timestamp so we avoid trying to pull from sim after deletion
		InProxy->SetSyncTimestamp(MarshallingManager.GetExternalTimestamp_External());

		RemoveDirtyProxy(InProxy);

		int32 NumRemoved = GeometryCollectionPhysicsProxies_External.Remove(InProxy);

		EnqueueCommandImmediate([InProxy, this]()
			{
				const TArray<Chaos::TPBDRigidClusteredParticleHandle<float, 3>*>& ParticleHandles = InProxy->GetSolverParticleHandles();
				for (const Chaos::TPBDRigidClusteredParticleHandle<float, 3> * ParticleHandle : ParticleHandles)
				{
					RemoveParticleToProxy(ParticleHandle);
				}
				GeometryCollectionPhysicsProxies_Internal.RemoveSingle(InProxy);
				InProxy->SyncBeforeDestroy();
				delete InProxy;
			});

		return NumRemoved == 1;
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::RegisterObject(Chaos::FJointConstraint* GTConstraint)
	{
		FJointConstraintPhysicsProxy* JointProxy = new FJointConstraintPhysicsProxy(GTConstraint, nullptr);
		JointProxy->SetSolver(this);


		JointConstraintPhysicsProxies_External.AddUnique(JointProxy);
		AddDirtyProxy(JointProxy);
	}

	template <typename Traits>
	bool TPBDRigidsSolver<Traits>::UnregisterObject(Chaos::FJointConstraint* GTConstraint)
	{
		FJointConstraintPhysicsProxy* JointProxy = GTConstraint->GetProxy<FJointConstraintPhysicsProxy>();
		check(JointProxy);

		RemoveDirtyProxy(JointProxy);

		// mark proxy timestamp so we avoid trying to pull from sim after deletion
		GTConstraint->GetProxy()->SetSyncTimestamp(MarshallingManager.GetExternalTimestamp_External());


		int32 NumRemoved = JointConstraintPhysicsProxies_External.Remove(JointProxy);
		GTConstraint->SetProxy(static_cast<FJointConstraintPhysicsProxy*>(nullptr));

		GTConstraint->ReleaseKinematicEndPoint(this);

		FParticlesType* InParticles = &GetParticles();

		// Finish registration on the physics thread...
		EnqueueCommandImmediate([InParticles, JointProxy, this]()
			{
				JointProxy->DestroyOnPhysicsThread(this);
				JointConstraintPhysicsProxies_Internal.RemoveSingle(JointProxy);
				delete JointProxy;
			});

		return NumRemoved == 1;
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::RegisterObject(Chaos::FSuspensionConstraint* GTConstraint)
	{
		FSuspensionConstraintPhysicsProxy* SuspensionProxy = new FSuspensionConstraintPhysicsProxy(GTConstraint, nullptr);
		SuspensionProxy->SetSolver(this);

		SuspensionConstraintPhysicsProxies.AddUnique(SuspensionProxy);
		AddDirtyProxy(SuspensionProxy);
	}

	template <typename Traits>
	bool TPBDRigidsSolver<Traits>::UnregisterObject(Chaos::FSuspensionConstraint* GTConstraint)
	{
		FSuspensionConstraintPhysicsProxy* SuspensionProxy = GTConstraint->GetProxy<FSuspensionConstraintPhysicsProxy>();
		check(SuspensionProxy);

		// mark proxy timestamp so we avoid trying to pull from sim after deletion
		SuspensionProxy->SetSyncTimestamp(MarshallingManager.GetExternalTimestamp_External());

		RemoveDirtyProxy(SuspensionProxy);

		int32 NumRemoved = SuspensionConstraintPhysicsProxies.Remove(SuspensionProxy);
		GTConstraint->SetProxy(static_cast<FSuspensionConstraintPhysicsProxy*>(nullptr));

		FParticlesType* InParticles = &GetParticles();

		// Finish registration on the physics thread...
		EnqueueCommandImmediate([InParticles, SuspensionProxy, this]()
			{
				SuspensionProxy->DestroyOnPhysicsThread(this);
				delete SuspensionProxy;
			});

		return NumRemoved == 1;
	}

	template <typename Traits>
	bool TPBDRigidsSolver<Traits>::IsSimulating() const
	{
		for (FGeometryParticlePhysicsProxy* Obj : GeometryParticlePhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FKinematicGeometryParticlePhysicsProxy* Obj : KinematicGeometryParticlePhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FRigidParticlePhysicsProxy* Obj : RigidParticlePhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FSkeletalMeshPhysicsProxy* Obj : SkeletalMeshPhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FStaticMeshPhysicsProxy* Obj : StaticMeshPhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		for (FGeometryCollectionPhysicsProxy* Obj : GeometryCollectionPhysicsProxies_External)
			if (Obj->IsSimulating())
				return true;
		for (FJointConstraintPhysicsProxy* Obj : JointConstraintPhysicsProxies_External)
			if (Obj->IsSimulating())
				return true;
		for (FSuspensionConstraintPhysicsProxy* Obj : SuspensionConstraintPhysicsProxies)
			if (Obj->IsSimulating())
				return true;
		return false;
	}

	int32 RewindCaptureNumFrames = -1;
	FAutoConsoleVariableRef CVarRewindCaptureNumFrames(TEXT("p.RewindCaptureNumFrames"),RewindCaptureNumFrames,TEXT("The number of frames to capture rewind for. Requires restart of solver"));

	int32 UseResimCache = 0;
	FAutoConsoleVariableRef CVarUseResimCache(TEXT("p.UseResimCache"),UseResimCache,TEXT("Whether resim uses cache to skip work, requires recreating world to take effect"));
	
	template <typename Traits>
	void TPBDRigidsSolver<Traits>::Reset()
	{
		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("PBDRigidsSolver::Reset()"));

		MTime = 0;
		MLastDt = 0.0f;
		CurrentFrame = 0;
		MMaxDeltaTime = 1.f;
		MMinDeltaTime = SMALL_NUMBER;
		MMaxSubSteps = 1;
		MEvolution = TUniquePtr<FPBDRigidsEvolution>(new FPBDRigidsEvolution(Particles, SimMaterials, &ContactModifiers, BufferMode == EMultiBufferMode::Single)); 

		PerSolverField = MakeUnique<FPerSolverFieldSystem>();

		//todo: do we need this?
		//MarshallingManager.Reset();

		if(RewindCaptureNumFrames >= 0)
		{
			EnableRewindCapture(RewindCaptureNumFrames, bUseCollisionResimCache || !!UseResimCache);
		}

		MEvolution->SetCaptureRewindDataFunction([this](const TParticleView<TPBDRigidParticles<FReal,3>>& ActiveParticles)
		{
			FinalizeRewindData(ActiveParticles);
		});

		TEventDefaults<Traits>::RegisterSystemEvents(*GetEventManager());
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::ChangeBufferMode(Chaos::EMultiBufferMode InBufferMode)
	{
		// This seems unused inside the solver? #BH
		BufferMode = InBufferMode;

		SetThreadingMode_External(BufferMode == EMultiBufferMode::Single ? EThreadingModeTemp::SingleThread : EThreadingModeTemp::TaskGraph);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::StartingSceneSimulation()
	{
		LLM_SCOPE(ELLMTag::Chaos);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_StartedSceneSimulation);

		if (HasActiveParticles())
		{
			GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager().PopStorageData_Internal(GetEvolution()->LatestExternalTimestampConsumed_Internal);
		}
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::DestroyPendingProxies_Internal()
	{
		const auto Helper = [](auto& Proxies)
		{
			for (auto Proxy : Proxies)
			{
				ensure(Proxy->GetHandle() == nullptr);	//should have already cleared this out
				delete Proxy;
			}
			Proxies.Reset();
		};
		
		Helper(PendingDestroyRigidProxy);
		Helper(PendingDestroyKinematicProxy);
		Helper(PendingDestroyGeometryProxy);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::AdvanceSolverBy(const FReal DeltaTime, const FSubStepInfo& SubStepInfo)
	{
		const FReal StartSimTime = GetSolverTime();
		MEvolution->GetCollisionDetector().GetNarrowPhase().GetContext().bDeferUpdate = (ChaosSolverCollisionDeferNarrowPhase != 0);
		MEvolution->GetCollisionDetector().GetNarrowPhase().GetContext().bAllowManifolds = (ChaosSolverCollisionUseManifolds != 0);
		MEvolution->GetCollisionDetector().GetNarrowPhase().GetContext().bUseIncrementalManifold = (ChaosSolverCollisionUseIncrememtalManifolds != 0);
		MEvolution->GetCollisionDetector().GetNarrowPhase().GetContext().bUseOneShotManifolds = (ChaosSolverCollisionUseOneShotManifolds != 0);

		// Apply CVAR overrides if set
		{
			if (ChaosSolverIterations >= 0)
			{
				SetIterations(ChaosSolverIterations);
			}
			if (ChaosSolverCollisionIterations >= 0)
			{
				SetCollisionPairIterations(ChaosSolverCollisionIterations);
			}
			if (ChaosSolverPushOutIterations >= 0)
			{
				SetPushOutIterations(ChaosSolverPushOutIterations);
			}
			if (ChaosSolverCollisionPushOutIterations >= 0)
			{
				SetCollisionPushOutPairIterations(ChaosSolverCollisionPushOutIterations);
			}
			if (ChaosSolverJointPairIterations >= 0.0f)
			{
				SetJointPairIterations(ChaosSolverJointPairIterations);
			}
			if (ChaosSolverJointPushOutPairIterations >= 0.0f)
			{
				SetJointPushOutPairIterations(ChaosSolverJointPushOutPairIterations);
			}
			if (ChaosSolverCullDistance >= 0.0f)
			{
				SetCollisionCullDistance(ChaosSolverCullDistance);
			}
		}

		UE_LOG(LogPBDRigidsSolver, Verbose, TEXT("PBDRigidsSolver::Tick(%3.5f)"), DeltaTime);
		MLastDt = DeltaTime;
		EventPreSolve.Broadcast(DeltaTime);
		AdvanceOneTimeStepTask<Traits>(this, DeltaTime, SubStepInfo).DoWork();

		if(DeltaTime > 0)
		{
			//pass information back to external thread
			//we skip dt=0 case because sync data should be identical if dt = 0
			MarshallingManager.FinalizePullData_Internal(MEvolution->LatestExternalTimestampConsumed_Internal, StartSimTime, DeltaTime);
		}

		if(SubStepInfo.Step == SubStepInfo.NumSteps - 1)
		{
			//final step so we can destroy proxies
			DestroyPendingProxies_Internal();
		}
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::SetExternalTimestampConsumed_Internal(const int32 Timestamp)
	{
		MEvolution->LatestExternalTimestampConsumed_Internal = Timestamp;
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::SyncEvents_GameThread()
	{
		GetEventManager()->DispatchEvents();
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::PushPhysicsState(const FReal DeltaTime, const int32 NumSteps, const int32 NumExternalSteps)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_PushPhysicsState);
		ensure(NumSteps > 0);
		ensure(NumExternalSteps > 0);
		//TODO: interpolate some data based on num steps

		FPushPhysicsData* PushData = MarshallingManager.GetProducerData_External();
		const FReal DynamicsWeight = FReal(1) / NumExternalSteps;
		FDirtySet* DirtyProxiesData = &PushData->DirtyProxiesDataBuffer;
		FDirtyPropertiesManager* Manager = &PushData->DirtyPropertiesManager;

		Manager->SetNumParticles(DirtyProxiesData->NumDirtyProxies());
		Manager->SetNumShapes(DirtyProxiesData->NumDirtyShapes());
		FShapeDirtyData* ShapeDirtyData = DirtyProxiesData->GetShapesDirtyData();
		auto ProcessProxyGT =[ShapeDirtyData, Manager, DirtyProxiesData](auto Proxy, int32 ParticleDataIdx, FDirtyProxy& DirtyProxy)
		{
			auto Particle = Proxy->GetParticle();
			Particle->SyncRemoteData(*Manager,ParticleDataIdx,DirtyProxy.ParticleData,DirtyProxy.ShapeDataIndices,ShapeDirtyData);
			Proxy->ClearAccumulatedData();
			Proxy->ResetDirtyIdx();
		};


		//todo: if we allocate remote data ahead of time we could go wide
		DirtyProxiesData->ParallelForEachProxy([&ProcessProxyGT, this, DynamicsWeight](int32 DataIdx, FDirtyProxy& Dirty)
		{
			switch(Dirty.Proxy->GetType())
			{
			case EPhysicsProxyType::SingleRigidParticleType:
			{
				auto Proxy = static_cast<FRigidParticlePhysicsProxy*>(Dirty.Proxy);
				Proxy->GetParticle()->ApplyDynamicsWeight(DynamicsWeight);
				ProcessProxyGT(Proxy,DataIdx,Dirty);
				break;
			}
			case EPhysicsProxyType::SingleKinematicParticleType:
			{
				auto Proxy = static_cast<FKinematicGeometryParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxyGT(Proxy,DataIdx,Dirty);
				break;
			}
			case EPhysicsProxyType::SingleGeometryParticleType:
			{
				auto Proxy = static_cast<FGeometryParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxyGT(Proxy,DataIdx,Dirty);
				break;
			}
			case EPhysicsProxyType::GeometryCollectionType:
			{
				// Not invalid but doesn't currently use the remote data process
				break;
			}
			case EPhysicsProxyType::JointConstraintType:
			{
				auto Proxy = static_cast<FJointConstraintPhysicsProxy*>(Dirty.Proxy);
				Proxy->PushStateOnGameThread(this);
				break;
			}
			case EPhysicsProxyType::SuspensionConstraintType:
			{
				auto Proxy = static_cast<FSuspensionConstraintPhysicsProxy*>(Dirty.Proxy);
				Proxy->PushStateOnGameThread(this);
				break;
			}

			default:
			ensure(0 && TEXT("Unknown proxy type in physics solver."));
			}
		});

		GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager().PushProducerStorageData_External(MarshallingManager.GetExternalTimestamp_External());

		MarshallingManager.Step_External(DeltaTime, NumSteps);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::ProcessSinglePushedData_Internal(FPushPhysicsData& PushData)
	{
		FRewindData* RewindData = GetRewindData();

		FDirtySet* DirtyProxiesData = &PushData.DirtyProxiesDataBuffer;
		FDirtyPropertiesManager* Manager = &PushData.DirtyPropertiesManager;
		FShapeDirtyData* ShapeDirtyData = DirtyProxiesData->GetShapesDirtyData();

		auto ProcessProxyPT = [Manager,ShapeDirtyData,RewindData, this](auto& Proxy,int32 DataIdx,FDirtyProxy& Dirty,const auto& CreateHandleFunc)
		{
			const bool bIsNew = !Proxy->IsInitialized();
			if(bIsNew)
			{
				const auto* NonFrequentData = Dirty.ParticleData.FindNonFrequentData(*Manager,DataIdx);
				const FUniqueIdx* UniqueIdx = NonFrequentData ? &NonFrequentData->UniqueIdx() : nullptr;
				Proxy->SetHandle(CreateHandleFunc(UniqueIdx));

				auto Handle = Proxy->GetHandle();
				Handle->GTGeometryParticle() = Proxy->GetParticle();
			}

			if(RewindData)
			{
				//may want to remove branch by templatizing lambda
				if(RewindData->IsResim())
				{
					RewindData->PushGTDirtyData<true>(*Manager,DataIdx,Dirty);
				} else
				{
					RewindData->PushGTDirtyData<false>(*Manager,DataIdx,Dirty);
				}
			}

			Proxy->PushToPhysicsState(*Manager,DataIdx,Dirty,ShapeDirtyData,*GetEvolution());

			if(bIsNew)
			{
				auto Handle = Proxy->GetHandle();
				AddParticleToProxy(Handle,Proxy);
				GetEvolution()->CreateParticle(Handle);
				Proxy->SetInitialized(true);
			}

			Dirty.Clear(*Manager,DataIdx,ShapeDirtyData);
		};

		if(RewindData)
		{
			RewindData->PrepareFrame(DirtyProxiesData->NumDirtyProxies());
		}

		//need to create new particle handles
		DirtyProxiesData->ForEachProxy([this,&ProcessProxyPT](int32 DataIdx,FDirtyProxy& Dirty)
		{
			switch(Dirty.Proxy->GetType())
			{
			case EPhysicsProxyType::SingleRigidParticleType:
			{
				auto Proxy = static_cast<FRigidParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxyPT(Proxy,DataIdx,Dirty,[this](const FUniqueIdx* UniqueIdx){ return Particles.CreateDynamicParticles(1,UniqueIdx)[0]; });
				break;
			}
			case EPhysicsProxyType::SingleKinematicParticleType:
			{
				auto Proxy = static_cast<FKinematicGeometryParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxyPT(Proxy,DataIdx,Dirty,[this](const FUniqueIdx* UniqueIdx){ return Particles.CreateKinematicParticles(1,UniqueIdx)[0]; });
				break;
			}
			case EPhysicsProxyType::SingleGeometryParticleType:
			{
				auto Proxy = static_cast<FGeometryParticlePhysicsProxy*>(Dirty.Proxy);
				ProcessProxyPT(Proxy,DataIdx,Dirty,[this](const FUniqueIdx* UniqueIdx){ return Particles.CreateStaticParticles(1,UniqueIdx)[0]; });
				break;
			}
			case EPhysicsProxyType::GeometryCollectionType:
			{
				// Currently no push needed for geometry collections and they handle the particle creation internally
				// #TODO This skips the rewind data push so GC will not be rewindable until resolved.
				Dirty.Proxy->ResetDirtyIdx();
				break;
			}
			case EPhysicsProxyType::JointConstraintType:
			case EPhysicsProxyType::SuspensionConstraintType:
			{
				// Pass until after all bodies are created. 
				break;
			}
			default:
			{
				ensure(0 && TEXT("Unknown proxy type in physics solver."));
				//Can't use, but we can still mark as "clean"
				Dirty.Proxy->ResetDirtyIdx();
			}

			}
		});

		//need to create new constraint handles
		DirtyProxiesData->ForEachProxy([this, &ProcessProxyPT](int32 DataIdx, FDirtyProxy& Dirty)
		{
			switch (Dirty.Proxy->GetType())
			{
			case EPhysicsProxyType::JointConstraintType:
			{
				auto JointProxy = static_cast<FJointConstraintPhysicsProxy*>(Dirty.Proxy);
				const bool bIsNew = !JointProxy->IsInitialized();
				if (bIsNew)
				{
					JointProxy->InitializeOnPhysicsThread(this);
					JointProxy->SetInitialized();
				}
				JointProxy->PushStateOnPhysicsThread(this);
				Dirty.Proxy->ResetDirtyIdx();
				break;
			}

			case EPhysicsProxyType::SuspensionConstraintType:
			{
				auto SuspensionProxy = static_cast<FSuspensionConstraintPhysicsProxy*>(Dirty.Proxy);
				const bool bIsNew = !SuspensionProxy->IsInitialized();
				if (bIsNew)
				{
					SuspensionProxy->InitializeOnPhysicsThread(this);
					SuspensionProxy->SetInitialized();
				}
				SuspensionProxy->PushStateOnPhysicsThread(this);
				Dirty.Proxy->ResetDirtyIdx();
				break;
			}

			}
		});

		{
			GetEvolution()->WakeIslands();
		}

		//MarshallingManager.FreeData_Internal(&PushData);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::ProcessPushedData_Internal(FPushPhysicsData& PushData)
	{
		//update callbacks
		SimCallbackObjects.Reserve(SimCallbackObjects.Num() + PushData.SimCallbackObjectsToAdd.Num());
		for(ISimCallbackObject* SimCallbackObject : PushData.SimCallbackObjectsToAdd)
		{
			SimCallbackObjects.Add(SimCallbackObject);
			if (SimCallbackObject->bContactModification)
			{
				ContactModifiers.Add(SimCallbackObject);
			}
		}

		//save any pending data for this particular interval
		for (const FSimCallbackInputAndObject& InputAndCallbackObj : PushData.SimCallbackInputs)
		{
			InputAndCallbackObj.CallbackObject->SetCurrentInput_Internal(InputAndCallbackObj.Input);
		}

		//remove any callbacks that are unregistered
		for (ISimCallbackObject* RemovedCallbackObject : PushData.SimCallbackObjectsToRemove)
		{
			RemovedCallbackObject->bPendingDelete = true;
		}

		for (int32 Idx = ContactModifiers.Num() - 1; Idx >= 0; --Idx)
		{
			ISimCallbackObject* Callback = ContactModifiers[Idx];
			if (Callback->bPendingDelete)
			{
				//will also be in SimCallbackObjects so we'll delete it in that loop
				ContactModifiers.RemoveAtSwap(Idx);
			}
		}

		for (int32 Idx = SimCallbackObjects.Num() - 1; Idx >= 0; --Idx)
		{
			ISimCallbackObject* Callback = SimCallbackObjects[Idx];
			if (Callback->bPendingDelete)
			{
				Callback->SetCurrentInput_Internal(nullptr);	//free any pending input
				delete Callback;
				SimCallbackObjects.RemoveAtSwap(Idx);
			}
		}

		ProcessSinglePushedData_Internal(PushData);

		//run any commands passed in. These don't generate outputs and are a one off so just do them here
		//note: commands run before sim callbacks. This is important for sub-stepping since we want each sub-step to have a consistent view
		//so for example if the user deletes a floor surface, we want all sub-steps to see that in the same way
		//also note, the commands run after data is marshalled over. This is important because data marshalling ensures any GT property changes are seen by command
		//for example a particle may not be created until marshalling occurs, and then a command could explicitly modify something like a collision setting
		for (FSimCallbackCommandObject* SimCallbackObject : PushData.SimCommands)
		{
			SimCallbackObject->PreSimulate_Internal();
		}
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::CompleteSceneSimulation()
	{
		LLM_SCOPE(ELLMTag::Chaos);
		SCOPE_CYCLE_COUNTER(STAT_BufferPhysicsResults);

		if(HasActiveParticles())
		{
			EventPreBuffer.Broadcast(MLastDt);
			GetDirtyParticlesBuffer()->CaptureSolverData(this);
			BufferPhysicsResults();
		}
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::BufferPhysicsResults()
	{
		//ensure(IsInPhysicsThread());
		TArray<FGeometryCollectionPhysicsProxy*> ActiveGC;
		ActiveGC.Reserve(GeometryCollectionPhysicsProxies_Internal.Num());

		FPullPhysicsData* PullData = MarshallingManager.GetCurrentPullData_Internal();

		TParticleView<TPBDRigidParticles<float, 3>>& DirtyParticles = GetParticles().GetDirtyParticlesView();

		//todo: should be able to go wide just add defaulted etc...
		{
			ensure(PullData->DirtyRigids.Num() == 0);	//we only fill this once per frame
			int32 BufferIdx = 0;
			PullData->DirtyRigids.Reserve(DirtyParticles.Num());

			for (Chaos::TPBDRigidParticleHandleImp<float, 3, false>& DirtyParticle : DirtyParticles)
			{
				if( const TSet<IPhysicsProxyBase*>* Proxies = GetProxies(DirtyParticle.Handle()))
				{
					for (IPhysicsProxyBase* Proxy : *Proxies)
					{
							if(Proxy != nullptr)
						{
								switch(DirtyParticle.GetParticleType())
							{
							case Chaos::EParticleType::Rigid:
								{
									PullData->DirtyRigids.AddDefaulted();
									((FRigidParticlePhysicsProxy*)(Proxy))->BufferPhysicsResults(PullData->DirtyRigids.Last());
									break;
								}
							case Chaos::EParticleType::Kinematic:
							case Chaos::EParticleType::Static:
								ensure(false);
								break;
							case Chaos::EParticleType::GeometryCollection:
								ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
								break;
							case Chaos::EParticleType::Clustered:
								ActiveGC.AddUnique((FGeometryCollectionPhysicsProxy*)(Proxy));
								break;
							default:
								check(false);
							}
						}
					}
				}
			}
		}

		{
			ensure(PullData->DirtyGeometryCollections.Num() == 0);	//we only fill this once per frame
			PullData->DirtyGeometryCollections.Reserve(ActiveGC.Num());

			for (int32 Idx = 0; Idx < ActiveGC.Num(); ++Idx)
			{
				PullData->DirtyGeometryCollections.AddDefaulted();
				ActiveGC[Idx]->BufferPhysicsResults(this, PullData->DirtyGeometryCollections.Last());
			}
		}

		{
			ensure(PullData->DirtyJointConstraints.Num() == 0);	//we only fill this once per frame
			PullData->DirtyJointConstraints.Reserve(JointConstraintPhysicsProxies_Internal.Num());

			for(int32 Idx = 0; Idx < JointConstraintPhysicsProxies_Internal.Num(); ++Idx)
			{
				PullData->DirtyJointConstraints.AddDefaulted();
				JointConstraintPhysicsProxies_Internal[Idx]->BufferPhysicsResults(PullData->DirtyJointConstraints.Last());
			}
		}
		

		
		// Now that results have been buffered we have completed a solve step so we can broadcast that event
		EventPostSolve.Broadcast(MLastDt);

	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::BeginDestroy()
	{
		MEvolution->SetCanStartAsyncTasks(false);
	}
	
	// This function is not called during normal Engine execution.  
	// FPhysScene_ChaosInterface::EndFrame() calls 
	// FPhysScene_ChaosInterface::SyncBodies() instead, and then immediately afterwards 
	// calls FPBDRigidsSovler::SyncEvents_GameThread().  This function is used by tests,
	// however.
	template <typename Traits>
	void TPBDRigidsSolver<Traits>::UpdateGameThreadStructures()
	{
		PullPhysicsStateForEachDirtyProxy_External([](auto){});
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::PostTickDebugDraw(FReal Dt) const
	{
#if CHAOS_DEBUG_DRAW
		if (ChaosSolverDebugDrawShapes == 1)
		{
			DebugDraw::DrawParticleShapes(FRigidTransform3(), Particles.GetActiveStaticParticlesView(), FColor(128, 0, 0), &ChaosSolverDebugDebugDrawSettings);
			DebugDraw::DrawParticleShapes(FRigidTransform3(), Particles.GetActiveKinematicParticlesView(), FColor(64, 32, 0), &ChaosSolverDebugDebugDrawSettings);
			DebugDraw::DrawParticleShapes(FRigidTransform3(), Particles.GetNonDisabledDynamicView(), FColor(255, 255, 0), &ChaosSolverDebugDebugDrawSettings);
		}
		if (ChaosSolverDebugDrawCollisions == 1) 
		{
			DebugDraw::DrawCollisions(FRigidTransform3(), GetEvolution()->GetCollisionConstraints(), 1.f, &ChaosSolverDebugDebugDrawSettings);
		}
		if (ChaosSolverDebugDrawBounds == 1)
		{
			DebugDraw::DrawParticleBounds(FRigidTransform3(), Particles.GetAllParticlesView(), Dt, GetEvolution()->GetBroadPhase().GetBoundsThickness(), GetEvolution()->GetBroadPhase().GetBoundsVelocityInflation(), &ChaosSolverDebugDebugDrawSettings);
		}
		if (ChaosSolverDrawTransforms == 1)
		{
			DebugDraw::DrawParticleTransforms(FRigidTransform3(), Particles.GetAllParticlesView(), &ChaosSolverDebugDebugDrawSettings);
		}
		if (ChaosSolverDrawIslands == 1)
		{
			DebugDraw::DrawConstraintGraph(FRigidTransform3(), GetEvolution()->GetCollisionConstraintsRule().GetGraphColor(), &ChaosSolverDebugDebugDrawSettings);
		}
#endif
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::PostEvolutionVDBPush() const
	{
#if CHAOS_VISUAL_DEBUGGER_ENABLED
		if (ChaosVisualDebuggerEnable)
		{
			const TGeometryParticleHandles<FReal, 3>&  AllParticleHandles = GetEvolution()->GetParticleHandles();
			for (uint32 ParticelIndex = 0; ParticelIndex < AllParticleHandles.Size(); ParticelIndex++)
			{
				const TUniquePtr<TGeometryParticleHandle<float, 3>>& ParticleHandle = AllParticleHandles.Handle(ParticelIndex);
				ChaosVisualDebugger::ParticlePositionLog(ParticleHandle->X());				
			}
		}
#endif
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::UpdateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData)
	{
		TSolverSimMaterialScope<ELockType::Write> Scope(this);
		*SimMaterials.Get(InHandle.InnerHandle) = InNewData;
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::CreateMaterial(Chaos::FMaterialHandle InHandle, const Chaos::FChaosPhysicsMaterial& InNewData)
	{
		TSolverSimMaterialScope<ELockType::Write> Scope(this);
		ensure(SimMaterials.Create(InNewData) == InHandle.InnerHandle);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::DestroyMaterial(Chaos::FMaterialHandle InHandle)
	{
		TSolverSimMaterialScope<ELockType::Write> Scope(this);
		SimMaterials.Destroy(InHandle.InnerHandle);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::UpdateMaterialMask(Chaos::FMaterialMaskHandle InHandle, const Chaos::FChaosPhysicsMaterialMask& InNewData)
	{
		TSolverSimMaterialScope<ELockType::Write> Scope(this);
		*SimMaterialMasks.Get(InHandle.InnerHandle) = InNewData;
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::CreateMaterialMask(Chaos::FMaterialMaskHandle InHandle, const Chaos::FChaosPhysicsMaterialMask& InNewData)
	{
		TSolverSimMaterialScope<ELockType::Write> Scope(this);
		ensure(SimMaterialMasks.Create(InNewData) == InHandle.InnerHandle);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::DestroyMaterialMask(Chaos::FMaterialMaskHandle InHandle)
	{
		TSolverSimMaterialScope<ELockType::Write> Scope(this);
		SimMaterialMasks.Destroy(InHandle.InnerHandle);
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::SyncQueryMaterials_External()
	{
		// Using lock on sim material is an imprefect workaround, we may block while physics thread is updating sim materials in callbacks.
		// QueryMaterials may be slightly stale. Need to rethink lifetime + ownership of materials for async case.
		//acquire external data lock
		FPhysicsSceneGuardScopedWrite ScopedWrite(GetExternalDataLock_External());
		TSolverSimMaterialScope<ELockType::Read> SimMatLock(this);
		
		QueryMaterials_External = SimMaterials;
		QueryMaterialMasks_External = SimMaterialMasks;
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::EnableRewindCapture(int32 NumFrames, bool InUseCollisionResimCache)
	{
		check(Traits::IsRewindable());
		MRewindData = MakeUnique<FRewindData>(NumFrames, InUseCollisionResimCache);
		bUseCollisionResimCache = InUseCollisionResimCache;
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::FinalizeRewindData(const TParticleView<TPBDRigidParticles<FReal,3>>& DirtyParticles)
	{
		using namespace Chaos;
		//Simulated objects must have their properties captured for rewind
		if(MRewindData && DirtyParticles.Num())
		{
			QUICK_SCOPE_CYCLE_COUNTER(RecordRewindData);

			MRewindData->PrepareFrameForPTDirty(DirtyParticles.Num());
			
			int32 DataIdx = 0;
			for(TPBDRigidParticleHandleImp<float,3,false>& DirtyParticle : DirtyParticles)
			{
				//may want to remove branch using templates outside loop
				if (MRewindData->IsResim())
				{
					MRewindData->PushPTDirtyData<true>(*DirtyParticle.Handle(), DataIdx++);
				}
				else
				{
					MRewindData->PushPTDirtyData<false>(*DirtyParticle.Handle(), DataIdx++);
				}
			}
		}
	}

	template <typename Traits>
	void TPBDRigidsSolver<Traits>::UpdateExternalAccelerationStructure_External(ISpatialAccelerationCollection<TAccelerationStructureHandle<FReal,3>,FReal,3>*& ExternalStructure)
	{
		GetEvolution()->UpdateExternalAccelerationStructure_External(ExternalStructure,*PendingSpatialOperations_External);
	}

	Chaos::FClusterCreationParameters<Chaos::FReal>::EConnectionMethod ToInternalConnectionMethod(EClusterUnionMethod InMethod)
	{
		using ETargetEnum = Chaos::FClusterCreationParameters<Chaos::FReal>::EConnectionMethod;
		switch(InMethod)
		{
		case EClusterUnionMethod::PointImplicit:
			return ETargetEnum::PointImplicit;
		case EClusterUnionMethod::DelaunayTriangulation:
			return ETargetEnum::DelaunayTriangulation;
		case EClusterUnionMethod::MinimalSpanningSubsetDelaunayTriangulation:
			return ETargetEnum::MinimalSpanningSubsetDelaunayTriangulation;
		case EClusterUnionMethod::PointImplicitAugmentedWithMinimalDelaunay:
			return ETargetEnum::PointImplicitAugmentedWithMinimalDelaunay;
		}

		return ETargetEnum::None;
	}

	template <typename Traits>
	void Chaos::TPBDRigidsSolver<Traits>::ApplyConfig(const FChaosSolverConfiguration& InConfig)
	{
		GetEvolution()->GetRigidClustering().SetClusterConnectionFactor(InConfig.ClusterConnectionFactor);
		GetEvolution()->GetRigidClustering().SetClusterUnionConnectionType(ToInternalConnectionMethod(InConfig.ClusterUnionConnectionType));
		SetIterations(InConfig.Iterations);
		SetCollisionPairIterations(InConfig.CollisionPairIterations);
		SetPushOutIterations(InConfig.PushOutIterations);
		SetCollisionPushOutPairIterations(InConfig.CollisionPushOutPairIterations);
		SetJointPairIterations(InConfig.JointPairIterations);
		SetJointPushOutPairIterations(InConfig.JointPushOutPairIterations);
		SetCollisionCullDistance(InConfig.CollisionCullDistance);
		SetGenerateCollisionData(InConfig.bGenerateCollisionData);
		SetGenerateBreakingData(InConfig.bGenerateBreakData);
		SetGenerateTrailingData(InConfig.bGenerateTrailingData);
		SetCollisionFilterSettings(InConfig.CollisionFilterSettings);
		SetBreakingFilterSettings(InConfig.BreakingFilterSettings);
		SetTrailingFilterSettings(InConfig.TrailingFilterSettings);
		SetUseContactGraph(InConfig.bGenerateContactGraph);
	}

#define EVOLUTION_TRAIT(Trait) template class CHAOS_API TPBDRigidsSolver<Trait>;
#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT

}; // namespace Chaos

