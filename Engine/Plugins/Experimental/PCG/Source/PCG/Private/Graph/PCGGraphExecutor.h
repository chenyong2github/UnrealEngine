// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGData.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGSubsystem.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionHandle.h" // Needed for FWorldPartitonReference
#endif

class UPCGGraph;
class UPCGNode;
class UPCGComponent;
class FPCGGraphCompiler;

struct FPCGGraphTask
{
	TArray<FPCGTaskId> Inputs;
	//TArray<DataId> Outputs;
	const UPCGNode* Node = nullptr;
	const UPCGComponent* SourceComponent = nullptr; // TODO: can technically be editor only
	FPCGElementPtr Element; // Added to have tasks that aren't node-bound
	FPCGTaskId NodeId = InvalidTaskId;
};

struct FPCGGraphScheduleTask
{
	TArray<FPCGGraphTask> Tasks;
};

struct FPCGGraphActiveTask
{
	FPCGElementPtr Element;
	FPCGContextPtr Context;
	FPCGTaskId NodeId = InvalidTaskId;
};

class FPCGGraphExecutor
{
public:
	FPCGGraphExecutor();
	~FPCGGraphExecutor();

	/** Compile (and cache) a graph for later use. This call is threadsafe */
	void Compile(UPCGGraph* InGraph);

	/** Schedules the execution of a given graph with specified inputs. This call is threadsafe */
	FPCGTaskId Schedule(UPCGComponent* InComponent, const TArray<FPCGTaskId>& TaskDependency = TArray<FPCGTaskId>());

#if WITH_EDITOR
	/** General job scheduling, used to control loading/unloading */
	FPCGTaskId ScheduleGeneric(TFunction<bool()> InOperation, const TArray<FPCGTaskId>& TaskDependencies);

	void AddToDirtyActors(AActor* Actor);
	void AddToUnusedActors(const TSet<FWorldPartitionReference>& UnusedActors);

	/** Notify compiler that graph has changed so it'll be removed from the cache */
	void NotifyGraphChanged(UPCGGraph* InGraph);
#endif

	/** "Tick" of the graph executor. This call is NOT THREADSAFE */
	void Execute();

private:
	void QueueReadyTasks(FPCGTaskId FinishedTaskHint = InvalidTaskId);
	void BuildTaskInput(const FPCGGraphTask& Task, FPCGDataCollection& TaskInput);
	void StoreResults(FPCGTaskId InTaskId, const FPCGDataCollection& InTaskOutput);
	void ClearResults();

#if WITH_EDITOR
	void SaveDirtyActors();
	void ReleaseUnusedActors();
#endif

	/** Graph compiler that turns a graph into tasks */
	TUniquePtr<FPCGGraphCompiler> GraphCompiler;

	FCriticalSection ScheduleLock;
	TArray<FPCGGraphScheduleTask> ScheduledTasks;

	TArray<FPCGGraphTask> Tasks;
	TArray<FPCGGraphTask> ReadyTasks;
	TArray<FPCGGraphActiveTask> ActiveTasks;
	TArray<UObject*> RootedData;
	/** Map of node instances to their output, could be cleared once execution is done */
	/** Note: this should at some point unload based on loaded/unloaded proxies, otherwise memory cost will be unbounded */
	TMap<FPCGTaskId, FPCGDataCollection> OutputData;
	/** Monotonically increasing id. Should be reset once all tasks are executed, should be protected by the ScheduleLock */
	FPCGTaskId NextTaskId = 0;

#if WITH_EDITOR
	FCriticalSection ActorsListLock;
	TSet<AActor*> ActorsToSave;
	TSet<FWorldPartitionReference> ActorsToRelease;
#endif
};

class FPCGFetchInputElement : public FSimplePCGElement
{
public:
	FPCGFetchInputElement(UPCGComponent* InComponent);

	virtual bool Execute(FPCGContextPtr Context) const override;
private:
	UPCGComponent* Component = nullptr;
};

#if WITH_EDITOR
class FPCGGenericElement : public FSimplePCGElement
{
public:
	FPCGGenericElement(TFunction<bool()> InOperation);

	// Important note: generic elements must always be run on the main thread
	// as most of these will impact the editor in some way (loading, unloading, saving)
	virtual bool Execute(FPCGContextPtr Context) const override;

private:
	TFunction<bool()> Operation;
};
#endif // WITH_EDITOR
