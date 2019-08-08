// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Physics/PhysScene.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GCObject.h"
#include "GameFramework/Actor.h"
#include "PhysicsPublic.h"
#include "PhysInterface_Chaos.h"
#include "Chaos/Transform.h"
#include "Chaos/Declares.h"
#include "Chaos/Framework/SingleParticlePhysicsProxyFwd.h"

#ifndef CHAOS_WITH_PAUSABLE_SOLVER
#define CHAOS_WITH_PAUSABLE_SOLVER 1
#endif

class UPrimitiveComponent;

class AdvanceOneTimeStepTask;
class FPhysInterface_Chaos;
class FChaosSolversModule;
struct FForceFieldProxy;
struct FSolverStateStorage;

class FSkeletalMeshPhysicsProxy;
class FStaticMeshPhysicsProxy;
class FGeometryCollectionPhysicsProxy;
class FFieldSystemPhysicsProxy;

class IPhysicsProxyBase;

namespace Chaos
{
	class FPhysicsProxy;
	class IDispatcher;

	enum EEventType;

	template<typename PayloadType, typename HandlerType>
	class TRawEventHandler;
}

/**
* Low level Chaos scene used when building custom simulations that don't exist in the main world physics scene.
*/
class ENGINE_API FPhysScene_Chaos : public FTickableGameObject, public FGCObject
{
public:

#if !WITH_CHAOS_NEEDS_TO_BE_FIXED
	FPhysScene_Chaos(AActor* InSolverActor);
#else
	FPhysScene_Chaos(AActor* InSolverActor=nullptr);
#endif

	virtual ~FPhysScene_Chaos();

	// Begin FTickableGameObject implementation
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(ChaosSolver, STATGROUP_Tickables); }
	// End FTickableGameObject

	/**
	 * Get the internal Chaos solver object
	 */
	Chaos::FPhysicsSolver* GetSolver() const;

	/** Returns the actor that owns this solver. */
	AActor* GetSolverActor() const;
	

	/**
	 * Get the internal Dispatcher object
	 */
	Chaos::IDispatcher* GetDispatcher() const;

	/**
	 * Called during creation of the physics state for gamethread objects to pass off an object to the physics thread
	 */
	void AddObject(UPrimitiveComponent* Component, FSkeletalMeshPhysicsProxy* InObject);
	void AddObject(UPrimitiveComponent* Component, FStaticMeshPhysicsProxy* InObject);
	void AddObject(UPrimitiveComponent* Component, FGeometryParticlePhysicsProxy* InObject);
	void AddObject(UPrimitiveComponent* Component, FGeometryCollectionPhysicsProxy* InObject);
	void AddObject(UPrimitiveComponent* Component, FFieldSystemPhysicsProxy* InObject);

	/**
	 * Called during physics state destruction for the game thread to remove objects from the simulation
	 * #BG TODO - Doesn't actually remove from the evolution at the moment
	 */
	void RemoveObject(FSkeletalMeshPhysicsProxy* InObject);
	void RemoveObject(FStaticMeshPhysicsProxy* InObject);
	void RemoveObject(FGeometryParticlePhysicsProxy* InObject);
	void RemoveObject(FGeometryCollectionPhysicsProxy* InObject);
	void RemoveObject(FFieldSystemPhysicsProxy* InObject);	

	template<typename PayloadType>
	void RegisterEvent(const Chaos::EEventType& EventID, TFunction<void(const Chaos::FPBDRigidsSolver* Solver, PayloadType& EventData)> InLambda);
	void UnregisterEvent(const Chaos::EEventType& EventID);

	template<typename PayloadType, typename HandlerType>
	void RegisterEventHandler(const Chaos::EEventType& EventID, HandlerType* Handler, typename Chaos::TRawEventHandler<PayloadType, HandlerType>::FHandlerFunction Func);
	void UnregisterEventHandler(const Chaos::EEventType& EventID, const void* Handler);

	void Shutdown();

#if WITH_EDITOR
	void AddPieModifiedObject(UObject* InObj);
#endif

	// FGCObject Interface ///////////////////////////////////////////////////
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//////////////////////////////////////////////////////////////////////////
	
	/** Given a solver object, returns its associated component. */
	template<class OwnerType>
	OwnerType* GetOwningComponent(IPhysicsProxyBase* PhysicsProxy) const
	{ 
		UPrimitiveComponent* const* CompPtr = PhysicsProxyToComponentMap.Find(PhysicsProxy);
		return CompPtr ? Cast<OwnerType>(*CompPtr) : nullptr;
	}

	/** Given a component, returns its associated solver object. */
	IPhysicsProxyBase* GetOwnedPhysicsProxy(UPrimitiveComponent* Comp) const
	{
		IPhysicsProxyBase* const* PhysicsProxyPtr = ComponentToPhysicsProxyMap.Find(Comp);
		return PhysicsProxyPtr ? *PhysicsProxyPtr : nullptr;
	}

private:
#if CHAOS_WITH_PAUSABLE_SOLVER
	/** Callback that checks the status of the world settings for this scene before pausing/unpausing its solver. */
	void OnUpdateWorldPause();
#endif

	void AddToComponentMaps(UPrimitiveComponent* Component, IPhysicsProxyBase* InObject);
	void RemoveFromComponentMaps(IPhysicsProxyBase* InObject);

#if WITH_EDITOR
	/**
	 * Callback when a world ends, to mark updated packages dirty. This can't be done in final
	 * sync as the editor will ignore packages being dirtied in PIE
	 */
	void OnWorldEndPlay();

	// List of objects that we modified during a PIE run for physics simulation caching.
	TArray<UObject*> PieModifiedObjects;
#endif

	// Control module for Chaos - cached to avoid constantly hitting the module manager
	FChaosSolversModule* ChaosModule;

	// Solver representing this scene
	Chaos::FPhysicsSolver* SceneSolver;

	// Maps PhysicsProxy to Component that created the PhysicsProxy
	TMap<IPhysicsProxyBase*, UPrimitiveComponent*> PhysicsProxyToComponentMap;

	// Maps Component to PhysicsProxy that is created
	TMap<UPrimitiveComponent*, IPhysicsProxyBase*> ComponentToPhysicsProxyMap;

	/** The SolverActor that spawned and owns this scene */
	TWeakObjectPtr<AActor> SolverActor;

#if WITH_EDITOR
	// Counter used to check a match with the single step status.
	int32 SingleStepCounter;
#endif

#if CHAOS_WITH_PAUSABLE_SOLVER
	// Cache the state of the game pause in order to avoid sending extraneous commands to the solver.
	bool bIsWorldPaused;
#endif
};

template<typename PayloadType>
void FPhysScene_Chaos::RegisterEvent(const Chaos::EEventType& EventID, TFunction<void(const Chaos::FPBDRigidsSolver* Solver, PayloadType& EventData)> InLambda)
{
	check(IsInGameThread());

	Chaos::IDispatcher* Dispatcher = GetDispatcher();
	Chaos::FPBDRigidsSolver* Solver = GetSolver();

	if (Dispatcher)
	{
		Dispatcher->EnqueueCommandImmediate([EventID, InLambda, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			InSolver->GetEventManager()->RegisterEvent<PayloadType>(InSolver, InLambda);
		});
	}
}

template<typename PayloadType, typename HandlerType>
void FPhysScene_Chaos::RegisterEventHandler(const Chaos::EEventType& EventID, HandlerType* Handler, typename Chaos::TRawEventHandler<PayloadType, HandlerType>::FHandlerFunction Func)
{
	check(IsInGameThread());

	Chaos::IDispatcher* Dispatcher = GetDispatcher();
	Chaos::FPBDRigidsSolver* Solver = GetSolver();

	if (Dispatcher)
	{
		Dispatcher->EnqueueCommandImmediate([EventID, Handler, Func, InSolver = Solver](Chaos::FPersistentPhysicsTask* PhysThread)
		{
			InSolver->GetEventManager()->RegisterHandler<PayloadType>(EventID, Handler, Func);
		});
	}
}

class UWorld;
class AWorldSettings;
class FPhysicsReplication;
class FPhysicsReplicationFactory;
class FContactModifyCallbackFactory;

#if WITH_CHAOS

class FPhysScene_ChaosInterface
{
public:

	friend class FPhysInterface_Chaos;

	ENGINE_API FPhysScene_ChaosInterface(const AWorldSettings* InSettings = nullptr);

	// Scene
	void OnWorldBeginPlay();
	void OnWorldEndPlay();

	// In Chaos, this function will update the pointers from actor handles to their proxies.
	// So the array of handles must be non-const.
	void ENGINE_API AddActorsToScene_AssumesLocked(TArray<FPhysicsActorHandle>& InActors);
	void AddAggregateToScene(const FPhysicsAggregateHandle& InAggregate);

	void SetOwningWorld(UWorld* InOwningWorld);

	UWorld* GetOwningWorld();
	const UWorld* GetOwningWorld() const;

	Chaos::FPhysicsSolver* GetSolver();
	const Chaos::FPhysicsSolver* GetSolver() const;

	FPhysicsReplication* GetPhysicsReplication();
	void RemoveBodyInstanceFromPendingLists_AssumesLocked(FBodyInstance* BodyInstance, int32 SceneType);
	void AddCustomPhysics_AssumesLocked(FBodyInstance* BodyInstance, FCalculateCustomPhysics& CalculateCustomPhysics);
	void AddForce_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, bool bAllowSubstepping, bool bAccelChange);
	void AddForceAtPosition_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Force, const FVector& Position, bool bAllowSubstepping, bool bIsLocalForce = false);
	void AddRadialForceToBody_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Origin, const float Radius, const float Strength, const uint8 Falloff, bool bAccelChange, bool bAllowSubstepping);
	void ClearForces_AssumesLocked(FBodyInstance* BodyInstance, bool bAllowSubstepping);
	void AddTorque_AssumesLocked(FBodyInstance* BodyInstance, const FVector& Torque, bool bAllowSubstepping, bool bAccelChange);
	void ClearTorques_AssumesLocked(FBodyInstance* BodyInstance, bool bAllowSubstepping);
	void SetKinematicTarget_AssumesLocked(FBodyInstance* BodyInstance, const FTransform& TargetTM, bool bAllowSubstepping);
	bool GetKinematicTarget_AssumesLocked(const FBodyInstance* BodyInstance, FTransform& OutTM) const;

	ENGINE_API void DeferredAddCollisionDisableTable(uint32 SkelMeshCompID, TMap<struct FRigidBodyIndexPair, bool> * CollisionDisableTable);
	ENGINE_API void DeferredRemoveCollisionDisableTable(uint32 SkelMeshCompID);

	void MarkForPreSimKinematicUpdate(USkeletalMeshComponent* InSkelComp, ETeleportType InTeleport, bool bNeedsSkinning);
	void ClearPreSimKinematicUpdate(USkeletalMeshComponent* InSkelComp);

	void AddPendingOnConstraintBreak(FConstraintInstance* ConstraintInstance, int32 SceneType);
	void AddPendingSleepingEvent(FBodyInstance* BI, ESleepEvent SleepEventType, int32 SceneType);

	TArray<FCollisionNotifyInfo>& GetPendingCollisionNotifies(int32 SceneType);

	ENGINE_API static bool SupportsOriginShifting();
	void ApplyWorldOffset(FVector InOffset);
	ENGINE_API void SetUpForFrame(const FVector* NewGrav, float InDeltaSeconds = 0.0f, float InMaxPhysicsDeltaTime = 0.0f);
	ENGINE_API void StartFrame();
	ENGINE_API void EndFrame(ULineBatchComponent* InLineBatcher);
	ENGINE_API void WaitPhysScenes();
	FGraphEventRef GetCompletionEvent();

	bool HandleExecCommands(const TCHAR* Cmd, FOutputDevice* Ar);
	void ListAwakeRigidBodies(bool bIncludeKinematic);
	ENGINE_API int32 GetNumAwakeBodies() const;

	ENGINE_API static TSharedPtr<IPhysicsReplicationFactory> PhysicsReplicationFactory;

	ENGINE_API void StartAsync();
	ENGINE_API bool HasAsyncScene() const;
	void SetPhysXTreeRebuildRate(int32 RebuildRate);
	ENGINE_API void EnsureCollisionTreeIsBuilt(UWorld* World);
	ENGINE_API void KillVisualDebugger();

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPhysScenePreTick, FPhysScene_ChaosInterface*, float /*DeltaSeconds*/);
	FOnPhysScenePreTick OnPhysScenePreTick;
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPhysSceneStep, FPhysScene_ChaosInterface*, float /*DeltaSeconds*/);
	FOnPhysSceneStep OnPhysSceneStep;
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPhysScenePostTick, FPhysScene_ChaosInterface*);
	FOnPhysScenePostTick OnPhysScenePostTick;

	ENGINE_API bool ExecPxVis(uint32 SceneType, const TCHAR* Cmd, FOutputDevice* Ar);
	ENGINE_API bool ExecApexVis(uint32 SceneType, const TCHAR* Cmd, FOutputDevice* Ar);

	template<typename PayloadType>
	void RegisterEvent(const Chaos::EEventType& EventID, TFunction<void(const Chaos::FPBDRigidsSolver* Solver, PayloadType& EventData)> InLambda)
	{
		Scene.RegisterEvent(EventID, InLambda);
	}
	void UnregisterEvent(const Chaos::EEventType& EventID)
	{
		Scene.UnregisterEvent(EventID);
	}

	template<typename PayloadType, typename HandlerType>
	void RegisterEventHandler(const Chaos::EEventType& EventID, HandlerType* Handler, typename Chaos::TRawEventHandler<PayloadType, HandlerType>::FHandlerFunction Func)
	{
		Scene.RegisterEventHandler<PayloadType, HandlerType>(EventID, Handler, Func);
	}
	void UnregisterEventHandler(const Chaos::EEventType& EventID, const void* Handler)
	{
		Scene.UnregisterEventHandler(EventID, Handler);
	}

private:

	void SyncBodies();
	void SyncBodies(Chaos::FPhysicsSolver* Solver);

	void SetKinematicTransform(FPhysicsActorHandle& InActorReference, const Chaos::TRigidTransform<float, 3>& NewTransform)
	{
		// #todo : Initialize
		// Set the buffered kinematic data on the game and render thread
		// InActorReference.GetPhysicsProxy()->SetKinematicData(...)
	}

	void Lock()
	{
		MCriticalSection.Lock();
	}

	void Unlock()
	{
		MCriticalSection.Unlock();
	}

	void EnableCollisionPair(const TTuple<int32, int32>& CollisionPair)
	{
		// #todo : Implement
	}

	void DisableCollisionPair(const TTuple<int32, int32>& CollisionPair)
	{
		// #todo : Implement
	}

	void SetGravity(const Chaos::TVector<float, 3>& Acceleration)
	{
		// #todo : Implement
	}

	FPhysicsConstraintReference_Chaos AddSpringConstraint(const TArray< TPair<FPhysicsActorHandle, FPhysicsActorHandle> >& Constraint);
	void RemoveSpringConstraint(const FPhysicsConstraintReference_Chaos& Constraint);

	void AddForce(const Chaos::TVector<float, 3>& Force, FPhysicsActorHandle& Handle)
	{
		// #todo : Implement
	}

	void AddTorque(const Chaos::TVector<float, 3>& Torque, FPhysicsActorHandle& Handle)
	{
		// #todo : Implement
	}


	void CompleteSceneSimulation(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

	FPhysScene_Chaos Scene;

	// @todo(mlentine): Locking is very heavy handed right now; need to make less so.
	FCriticalSection MCriticalSection;
	float MDeltaTime;
	//Body Instances
	Chaos::TArrayCollectionArray<FBodyInstance*> BodyInstances;
	// Temp Interface
	UWorld* MOwningWorld;
	TArray<FCollisionNotifyInfo> MNotifies;

	// Taskgraph control
	FGraphEventRef CompletionEvent;
	FGraphEventRef PhysicsTickTask;
};

#endif

#endif
