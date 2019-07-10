// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if INCLUDE_CHAOS

#include "CoreMinimal.h"
#include "Tickable.h"
#include "Physics/PhysScene.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GCObject.h"
#include "SolverObjects/SolverObject.h"
#include "GameFramework/Actor.h"
#include "PhysicsPublic.h"
#include "PhysInterface_Chaos.h"
#include "Chaos/Transform.h"
#include "Chaos/PBDSpringConstraints.h"
#include "Chaos/PerParticleGravity.h"

#ifndef CHAOS_WITH_PAUSABLE_SOLVER
#define CHAOS_WITH_PAUSABLE_SOLVER 1
#endif

class UPrimitiveComponent;

class AdvanceOneTimeStepTask;
class FPhysInterface_Chaos;
class FChaosSolversModule;
struct FForceFieldProxy;
struct FSolverStateStorage;

class FSkeletalMeshPhysicsObject;
class FStaticMeshPhysicsObject;
class FBodyInstancePhysicsObject;
class FGeometryCollectionPhysicsObject;
class FFieldSystemPhysicsObject;

namespace Chaos
{
	class FPBDRigidsSolver;
	class FPhysicsProxy;
	class IDispatcher;

	template <typename T, int>
	class PerParticleGravity;

	template <typename T, int>
	class TPBDSpringConstraints;
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
	Chaos::FPBDRigidsSolver* GetSolver() const;

	/** Returns the actor that owns this solver. */
	AActor* GetSolverActor() const;
	

	/**
	 * Get the internal Dispatcher object
	 */
	Chaos::IDispatcher* GetDispatcher() const;

	/**
	 * Called during creation of the physics state for gamethread objects to pass off an object to the physics thread
	 */
	void AddObject(UPrimitiveComponent* Component, FSkeletalMeshPhysicsObject* InObject);
	void AddObject(UPrimitiveComponent* Component, FStaticMeshPhysicsObject* InObject);
	void AddObject(UPrimitiveComponent* Component, FBodyInstancePhysicsObject* InObject);
	void AddObject(UPrimitiveComponent* Component, FGeometryCollectionPhysicsObject* InObject);
	void AddObject(UPrimitiveComponent* Component, FFieldSystemPhysicsObject* InObject);

	/**
	 * Called during physics state destruction for the game thread to remove objects from the simulation
	 * #BG TODO - Doesn't actually remove from the evolution at the moment
	 */
	void RemoveObject(FSkeletalMeshPhysicsObject* InObject);
	void RemoveObject(FStaticMeshPhysicsObject* InObject);
	void RemoveObject(FBodyInstancePhysicsObject* InObject);
	void RemoveObject(FGeometryCollectionPhysicsObject* InObject);
	void RemoveObject(FFieldSystemPhysicsObject* InObject);	

	void Shutdown();

#if WITH_EDITOR
	void AddPieModifiedObject(UObject* InObj);
#endif

	// FGCObject Interface ///////////////////////////////////////////////////
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//////////////////////////////////////////////////////////////////////////
	
	/** Given a solver object, returns its associated component. */
	template<class OwnerType>
	OwnerType* GetOwningComponent(ISolverObjectBase* SolverObject) const
	{ 
		UPrimitiveComponent* const* CompPtr = SolverObjectToComponentMap.Find(SolverObject);
		return CompPtr ? Cast<OwnerType>(*CompPtr) : nullptr;
	}

	/** Given a component, returns its associated solver object. */
	ISolverObjectBase* GetOwnedSolverObject(UPrimitiveComponent* Comp) const
	{
		ISolverObjectBase* const* SolverObjectPtr = ComponentToSolverObjectMap.Find(Comp);
		return SolverObjectPtr ? *SolverObjectPtr : nullptr;
	}

private:
#if CHAOS_WITH_PAUSABLE_SOLVER
	/** Callback that checks the status of the world settings for this scene before pausing/unpausing its solver. */
	void OnUpdateWorldPause();
#endif

	void AddToComponentMaps(UPrimitiveComponent* Component, ISolverObjectBase* InObject);
	void RemoveFromComponentMaps(ISolverObjectBase* InObject);

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
	Chaos::FPBDRigidsSolver* SceneSolver;

	// Maps SolverObject to Component that created the SolverObject
	TMap<ISolverObjectBase*, UPrimitiveComponent*> SolverObjectToComponentMap;

	// Maps Component to SolverObject that is created
	TMap<UPrimitiveComponent*, ISolverObjectBase*> ComponentToSolverObjectMap;

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

	void AddActorsToScene_AssumesLocked(const TArray<FPhysicsActorHandle>& InActors);
	void AddAggregateToScene(const FPhysicsAggregateHandle& InAggregate);

	void SetOwningWorld(UWorld* InOwningWorld);

	UWorld* GetOwningWorld();
	const UWorld* GetOwningWorld() const;

	Chaos::FPBDRigidsSolver* GetSolver();
	const Chaos::FPBDRigidsSolver* GetSolver() const;

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

private:

	void SyncBodies();

	void SetKinematicTransform(FPhysicsActorReference_Chaos& InActorReference, const Chaos::TRigidTransform<float, 3>& NewTransform)
	{
		// #todo : Initialize
		// Set the buffered kinematic data on the game and render thread
		// InActorReference.GetPhysicsObject()->SetKinematicData(...)
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

	FPhysicsConstraintReference_Chaos AddSpringConstraint(const TArray< TPair<FPhysicsActorReference_Chaos, FPhysicsActorReference_Chaos> >& Constraint);
	void RemoveSpringConstraint(const FPhysicsConstraintReference_Chaos& Constraint);

	void AddForce(const Chaos::TVector<float, 3>& Force, FPhysicsActorReference_Chaos& Handle)
	{
		// #todo : Implement
	}

	void AddTorque(const Chaos::TVector<float, 3>& Torque, FPhysicsActorReference_Chaos& Handle)
	{
		// #todo : Implement
	}


	void CompleteSceneSimulation(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

	FPhysScene_Chaos Scene;

	// @todo(mlentine): Locking is very heavy handed right now; need to make less so.
	FCriticalSection MCriticalSection;
	float MDeltaTime;
	TUniquePtr<Chaos::PerParticleGravity<float, 3>> MGravity;
	//Springs
	TUniquePtr<Chaos::TPBDSpringConstraints<float, 3>> MSpringConstraints;
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
