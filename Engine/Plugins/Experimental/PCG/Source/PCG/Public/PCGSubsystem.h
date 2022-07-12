// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "PCGCommon.h"
#include "PCGComponent.h"

#include "PCGSubsystem.generated.h"

class FPCGGraphExecutor;
class APCGPartitionActor;
class APCGWorldActor;
class UPCGGraph;
struct FPCGDataCollection;

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
	void RegisterPCGWorldActor(APCGWorldActor* InActor);
	void UnregisterPCGWorldActor(APCGWorldActor* InActor);

	// Schedule graph (owner -> graph)
	FPCGTaskId ScheduleComponent(UPCGComponent* PCGComponent, const TArray<FPCGTaskId>& Dependencies);

	// Schedule cleanup (owner -> graph)
	FPCGTaskId ScheduleCleanup(UPCGComponent* PCGComponent, bool bRemoveComponents, const TArray<FPCGTaskId>& Dependencies);

	// Schedule multiple graphs
	TArray<FPCGTaskId> ScheduleMultipleComponent(UPCGComponent* OriginalComponent, TSet<TSoftObjectPtr<APCGPartitionActor>>& PartitionActors, const TArray<FPCGTaskId>& Dependencies);

	// Schedule multiple cleanups
	TArray<FPCGTaskId> ScheduleMultipleCleanup(UPCGComponent* OriginalComponent, TSet<TSoftObjectPtr<APCGPartitionActor>>& PartitionActors, bool bRemoveComponents, const TArray<FPCGTaskId>& Dependencies);

	// Schedule graph (used internally for dynamic subgraph execution)
	FPCGTaskId ScheduleGraph(UPCGGraph* Graph, UPCGComponent* SourceComponent, FPCGElementPtr InputElement, const TArray<FPCGTaskId>& Dependencies);

	/** General job scheduling, used to control loading/unloading */
	FPCGTaskId ScheduleGeneric(TFunction<bool()> InOperation, const TArray<FPCGTaskId>& TaskDependencies);

	/** Gets the output data for a given task */
	bool GetOutputData(FPCGTaskId InTaskId, FPCGDataCollection& OutData);

#if WITH_EDITOR
public:
	/** Operations to schedule a later operation, which enables to delay bounds querying */
	void DelayPartitionGraph(UPCGComponent* Component);
	void DelayUnpartitionGraph(UPCGComponent* Component);
	FPCGTaskId DelayGenerateGraph(UPCGComponent* Component, bool bSave);

	/** Schedules an operation to cleanup the graph in the given bounds */
	void CleanupGraph(UPCGComponent* Component, const FBox& InBounds, bool bRemoveComponents, bool bSave);

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

private:
	FPCGTaskId DelayProcessGraph(UPCGComponent* Component, bool bGenerate, bool bSave, bool bUseEmptyNewBounds);
	FPCGTaskId ProcessGraph(UPCGComponent* Component, const FBox& InPreviousBounds, const FBox& InNewBounds, bool bGenerate, bool bSave);
#endif // WITH_EDITOR
	
private:
	APCGWorldActor* PCGWorldActor = nullptr;
	FPCGGraphExecutor* GraphExecutor = nullptr;
};