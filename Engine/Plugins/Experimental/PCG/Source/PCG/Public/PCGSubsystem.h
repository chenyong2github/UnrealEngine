// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "PCGSubsystem.generated.h"

class UPCGComponent;
class FPCGGraphExecutor;
class APCGPartitionActor;
class APCGWorldActor;
class UPCGGraph;

using FPCGTaskId = uint64;
static const FPCGTaskId InvalidTaskId = (uint64)-1;

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

#if WITH_EDITOR
public:
	/** Operations to schedule a later operation, which enables to delay bounds querying */
	void DelayPartitionGraph(UPCGComponent* Component);
	void DelayUnpartitionGraph(UPCGComponent* Component);
	FPCGTaskId DelayGenerateGraph(UPCGComponent* Component, bool bSave);

	/** Schedules an operation to cleanup the graph in the given bounds */
	void CleanupGraph(UPCGComponent* Component, const FBox& InBounds, bool bRemoveComponents, bool bSave);

	/** Immediately dirties the partition actors in the given bounds */
	void DirtyGraph(UPCGComponent* Component, const FBox& InBounds, bool bDirtyInputs, bool bDirtyExclusions);

	/** Partition actors methods */
	void CleanupPartitionActors(const FBox& InBounds);
	void DeletePartitionActors();

	/** General job scheduling, used to control loading/unloading */
	FPCGTaskId ScheduleGeneric(TFunction<bool()> InOperation, const TArray<FPCGTaskId>& TaskDependencies);

	/** Propagate to the graph compiler graph changes */
	void NotifyGraphChanged(UPCGGraph* InGraph);

private:
	FPCGTaskId DelayProcessGraph(UPCGComponent* Component, bool bGenerate, bool bSave, bool bUseEmptyNewBounds);
	FPCGTaskId ProcessGraph(UPCGComponent* Component, const FBox& InPreviousBounds, const FBox& InNewBounds, bool bGenerate, bool bSave);
#endif // WITH_EDITOR
	
private:
	APCGWorldActor* PCGWorldActor = nullptr;
	FPCGGraphExecutor* GraphExecutor = nullptr;
};