// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "PCGCommon.h"
#include "PCGComponent.h"
#include "PCGVolume.h"
#include "Grid/PCGComponentOctree.h"

#include "PCGSubsystem.generated.h"

class FPCGGraphExecutor;
class APCGPartitionActor;
class APCGWorldActor;
class UPCGGraph;
struct FPCGDataCollection;
struct FPCGLandscapeCache;

class IPCGElement;
typedef TSharedPtr<IPCGElement, ESPMode::ThreadSafe> FPCGElementPtr;


/**
* UPCGSubsystem
*/
UCLASS()
class PCG_API UPCGSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface.
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

	//~ Begin UWorldSubsystem Interface.
	virtual void PostInitialize() override;
	// need UpdateStreamingState? 
	//~ End UWorldSubsystem Interface.

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	APCGWorldActor* GetPCGWorldActor();
#if WITH_EDITOR
	void DestroyPCGWorldActor();
#endif
	void RegisterPCGWorldActor(APCGWorldActor* InActor);
	void UnregisterPCGWorldActor(APCGWorldActor* InActor);

	FPCGLandscapeCache* GetLandscapeCache();

	// Schedule graph (owner -> graph)
	FPCGTaskId ScheduleComponent(UPCGComponent* PCGComponent, bool bSave, const TArray<FPCGTaskId>& Dependencies);

	/** Schedule cleanup(owner->graph). Note that in non-partitioned mode, cleanup is immediate. */
	FPCGTaskId ScheduleCleanup(UPCGComponent* PCGComponent, bool bRemoveComponents, bool bSave, const TArray<FPCGTaskId>& Dependencies);

	// Schedule graph (used internally for dynamic subgraph execution)
	FPCGTaskId ScheduleGraph(UPCGGraph* Graph, UPCGComponent* SourceComponent, FPCGElementPtr InputElement, const TArray<FPCGTaskId>& Dependencies);

	/** General job scheduling, used to control loading/unloading */
	FPCGTaskId ScheduleGeneric(TFunction<bool()> InOperation, const TArray<FPCGTaskId>& TaskDependencies);

	/** Gets the output data for a given task */
	bool GetOutputData(FPCGTaskId InTaskId, FPCGDataCollection& OutData);

	/** Register a new PCG Component, will be added to the octree. Returns an id that needs to be kept for update and removal. Thread safe */
	void RegisterPCGComponent(UPCGComponent* InComponent);

	/** Update a PCG Component, if it has changed its transform. Thread safe */
	void UpdatePCGComponentBounds(UPCGComponent* InComponent);

	/** Unregister a PCG Component, will be removed from the octree. Thread safe */
	void UnregisterPCGComponent(UPCGComponent* InComponent);

	/** Register a new Partition actor, will be added to a map and will query all intersecting volume to bind to them. Thread safe */
	void RegisterPartitionActor(APCGPartitionActor* InActor);

	/** Unregister a Partition actor, will be removed from the map and remove itself to all intersecting volumes. Thread safe */
	void UnregisterPartitionActor(APCGPartitionActor* InActor);

#if WITH_EDITOR
public:
	/** Operations to schedule a later operation, which enables to delay bounds querying */
	void DelayPartitionGraph(UPCGComponent* Component);
	void DelayUnpartitionGraph(UPCGComponent* Component);
	FPCGTaskId DelayGenerateGraph(UPCGComponent* Component, bool bSave);

	/** Schedules an operation to cleanup the graph in the given bounds */
	FPCGTaskId CleanupGraph(UPCGComponent* Component, const FBox& InBounds, bool bRemoveComponents, bool bSave);

	/** Immediately dirties the partition actors in the given bounds */
	void DirtyGraph(UPCGComponent* Component, const FBox& InBounds, EPCGComponentDirtyFlag DirtyFlag);

	/** Partition actors methods */
	void CleanupPartitionActors(const FBox& InBounds);
	void DeletePartitionActors();

	/** Propagate to the graph compiler graph changes */
	void NotifyGraphChanged(UPCGGraph* InGraph);

	/** Cleans up the graph cache on an element basis */
	void CleanFromCache(const IPCGElement* InElement);

	/** Flushes the graph cache completely, use only for debugging */
	void FlushCache();

	/** Move all resources from sub actors to a new actor */
	void ClearPCGLink(UPCGComponent* InComponent, const FBox& InBounds, AActor* InNewActor);

	/** If the partition grid size change, call this to empty the Partition actors map */
	void ResetPartitionActorsMap();

	/** Builds the landscape data cache */
	void BuildLandscapeCache();

	/** Clears the landscape data cache */
	void ClearLandscapeCache();

private:
	enum class EOperation : uint32
	{
		Partition,
		Unpartition,
		Generate
	};

	FPCGTaskId DelayProcessGraph(UPCGComponent* Component, EOperation InOperation, bool bSave);
	FPCGTaskId ProcessGraph(UPCGComponent* Component, const FBox& InPreviousBounds, const FBox& InNewBounds, EOperation InOperation, bool bSave);
#endif // WITH_EDITOR
	
private:
	// Schedule multiple graphs
	TArray<FPCGTaskId> ScheduleMultipleComponent(UPCGComponent* OriginalComponent, TSet<TObjectPtr<APCGPartitionActor>>& PartitionActors, const TArray<FPCGTaskId>& Dependencies);

	// Schedule multiple cleanups
	TArray<FPCGTaskId> ScheduleMultipleCleanup(UPCGComponent* OriginalComponent, TSet<TObjectPtr<APCGPartitionActor>>& PartitionActors, bool bRemoveComponents, const TArray<FPCGTaskId>& Dependencies);

	/** Iterate other all the components which bounds intersect the box in param and call a callback. Thread safe */
	void FindAllIntersectingComponents(const FBoxCenterAndExtent& InBounds, TFunction<void(UPCGComponent*)> InFunc) const;

	/** Iterate other all the int coordinates given a box and call a callback. Thread safe */
	void FindAllIntersectingPartitionActors(const FBox& InBounds, TFunction<void(APCGPartitionActor*)> InFunc) const;
	
private:
	APCGWorldActor* PCGWorldActor = nullptr;
	FPCGGraphExecutor* GraphExecutor = nullptr;

#if WITH_EDITOR
	FCriticalSection PCGWorldActorLock;
#endif

	FPCGComponentOctree PCGComponentOctree;
	TMap<TObjectPtr<const UPCGComponent>, FPCGComponentOctreeIDSharedRef> ComponentToIdMap;
	mutable FRWLock PCGVolumeOctreeLock;

	TMap<FIntVector, TObjectPtr<APCGPartitionActor>> PartitionActorsMap;
	mutable FRWLock PartitionActorsMapLock;

	TMap<TObjectPtr<const UPCGComponent>, TSet<TObjectPtr<APCGPartitionActor>>> ComponentToPartitionActorsMap;
	mutable FRWLock ComponentToPartitionActorsMapLock;
};