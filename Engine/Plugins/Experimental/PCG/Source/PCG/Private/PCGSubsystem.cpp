// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGSubsystem.h"
#include "PCGComponent.h"
#include "PCGWorldActor.h"
#include "Graph/PCGGraphExecutor.h"
#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGActorHelpers.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Engine.h"
#include "ActorPartition/ActorPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/ActorPartition/PartitionActorDesc.h"
#include "ObjectTools.h"
#endif

#if WITH_EDITOR
namespace PCGSubsystemConsole
{
	static FAutoConsoleCommand CommandFlushCache(
		TEXT("pcg.FlushCache"),
		TEXT("Clears the PCG results cache."),
		FConsoleCommandDelegate::CreateLambda([]()
			{
				if (GEditor)
				{
					if (UWorld* World = GEditor->GetEditorWorldContext().World())
					{
						World->GetSubsystem<UPCGSubsystem>()->FlushCache();
					}
				}
			}));
}
#endif

void UPCGSubsystem::Deinitialize()
{
	// Cancel all tasks
	// TODO
	delete GraphExecutor;
	GraphExecutor = nullptr;

	Super::Deinitialize();
}

void UPCGSubsystem::PostInitialize()
{
	Super::PostInitialize();

	// Initialize graph executor
	check(!GraphExecutor);
	GraphExecutor = new FPCGGraphExecutor(this);
	// gather things.. ?
	// TODO	
}

void UPCGSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// If we have any tasks to execute, schedule some
	GraphExecutor->Execute();
}

APCGWorldActor* UPCGSubsystem::GetPCGWorldActor()
{
#if WITH_EDITOR
	if (!PCGWorldActor)
	{
		PCGWorldActor = APCGWorldActor::CreatePCGWorldActor(GetWorld());
	}
#endif

	return PCGWorldActor;
}

void UPCGSubsystem::RegisterPCGWorldActor(APCGWorldActor* InActor)
{
	check(!PCGWorldActor || PCGWorldActor == InActor);
	PCGWorldActor = InActor;
}

void UPCGSubsystem::UnregisterPCGWorldActor(APCGWorldActor* InActor)
{
	check(PCGWorldActor == InActor);
	if (PCGWorldActor == InActor)
	{
		PCGWorldActor = nullptr;
	}
}

FPCGTaskId UPCGSubsystem::ScheduleComponent(UPCGComponent* PCGComponent, const TArray<FPCGTaskId>& Dependencies)
{
	check(GraphExecutor);

	FPCGTaskId ProcessTaskId = PCGComponent ? GraphExecutor->Schedule(PCGComponent, Dependencies) : InvalidPCGTaskId;

	if (ProcessTaskId != InvalidPCGTaskId)
	{
#if WITH_EDITOR
		TWeakObjectPtr<UPCGComponent> ComponentPtr(PCGComponent);

		return GraphExecutor->ScheduleGeneric([ComponentPtr]() {
			if (UPCGComponent* Component = ComponentPtr.Get())
			{
				const FBox NewBounds = Component->GetGridBounds();
				Component->PostProcessGraph(NewBounds, /*bGenerate=*/true);
			}

			return true;
			}, { ProcessTaskId });
#else
		return ProcessTaskId;
#endif
	}
	else
	{
		UE_LOG(LogPCG, Error, TEXT("[ScheduleComponent] Didn't schedule any task."));
		if (PCGComponent)
		{
			PCGComponent->OnProcessGraphAborted();
		}
		return InvalidPCGTaskId;
	}
}

FPCGTaskId UPCGSubsystem::ScheduleGraph(UPCGGraph* Graph, UPCGComponent* SourceComponent, FPCGElementPtr InputElement, const TArray<FPCGTaskId>& Dependencies)
{
	if (SourceComponent)
	{
		return GraphExecutor->Schedule(Graph, SourceComponent, InputElement, Dependencies);
	}
	else
	{
		return InvalidPCGTaskId;
	}
}

ETickableTickType UPCGSubsystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always;
}

TStatId UPCGSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPCGSubsystem, STATGROUP_Tickables);
}

FPCGTaskId UPCGSubsystem::ScheduleGeneric(TFunction<bool()> InOperation, const TArray<FPCGTaskId>& TaskDependencies)
{
	check(GraphExecutor);
	return GraphExecutor->ScheduleGeneric(InOperation, TaskDependencies);
}

bool UPCGSubsystem::GetOutputData(FPCGTaskId TaskId, FPCGDataCollection& OutData)
{
	check(GraphExecutor);
	return GraphExecutor->GetOutputData(TaskId, OutData);
}

#if WITH_EDITOR

namespace PCGSubsystem
{
	FPCGTaskId ForEachIntersectingCell(FPCGGraphExecutor* GraphExecutor, UWorld* World, const FBox& InBounds, bool bCreateActor, bool bLoadCell, bool bSaveActors, TFunctionRef<FPCGTaskId(APCGPartitionActor*, const FBox&, const TArray<FPCGTaskId>&)> InOperation)
	{
		if (!GraphExecutor || !World)
		{
			UE_LOG(LogPCG, Error, TEXT("[ForEachIntersectingCell] GraphExecutor or World is null"));
			return InvalidPCGTaskId;
		}

		TArray<FPCGTaskId> CellTasks;

		auto CellLambda = [&CellTasks, GraphExecutor, World, bCreateActor, bLoadCell, bSaveActors, InBounds, &InOperation](const UActorPartitionSubsystem::FCellCoord& CellCoord, const FBox& CellBounds)
		{
			UActorPartitionSubsystem* PartitionSubsystem = UWorld::GetSubsystem<UActorPartitionSubsystem>(World);
			FBox IntersectedBounds = InBounds.Overlap(CellBounds);

			if (IntersectedBounds.IsValid)
			{
				TSharedPtr<TSet<FWorldPartitionReference>> ActorReferences = MakeShared<TSet<FWorldPartitionReference>>();

				auto PostCreation = [](APartitionActor* Actor) { CastChecked<APCGPartitionActor>(Actor)->PostCreation(); };

				const bool bInBoundsSearch = true;
				const FGuid DefaultGridGuid;
				const uint32 DefaultGridSize = 0;

				APCGPartitionActor* PCGActor = Cast<APCGPartitionActor>(PartitionSubsystem->GetActor(APCGPartitionActor::StaticClass(), CellCoord, bCreateActor, DefaultGridGuid, DefaultGridSize, bInBoundsSearch, PostCreation));

				// At this point, if bCreateActor was true, then it exists, but it is not currently loaded; make sure it is loaded
				// Otherwise, we still need to load it if it exists
				// TODO: Revisit after API review on the WP side, we shouldn't have to load here or get the actor desc directly
				if (!PCGActor && bSaveActors)
				{
					const FWorldPartitionActorDesc* PCGActorDesc = nullptr;
					auto FindFirst = [&CellCoord, &PCGActorDesc](const FWorldPartitionActorDesc* ActorDesc) {
						FPartitionActorDesc* PartitionActorDesc = (FPartitionActorDesc*)ActorDesc;

						if (PartitionActorDesc &&
							PartitionActorDesc->GridIndexX == CellCoord.X &&
							PartitionActorDesc->GridIndexY == CellCoord.Y &&
							PartitionActorDesc->GridIndexZ == CellCoord.Z)
						{
							PCGActorDesc = ActorDesc;
							return false;
						}
						else
						{
							return true;
						}
					};

					FWorldPartitionHelpers::ForEachIntersectingActorDesc<APCGPartitionActor>(World->GetWorldPartition(), CellBounds, FindFirst);

					check(!bCreateActor || PCGActorDesc);
					if (PCGActorDesc)
					{
						ActorReferences->Add(FWorldPartitionReference(World->GetWorldPartition(), PCGActorDesc->GetGuid()));
						PCGActor = Cast<APCGPartitionActor>(PCGActorDesc->GetActor());
					}
				}
				else if(PCGActor)
				{
					// We still need to keep a reference on the PCG actor - note that newly created PCG actors will not have a reference here, but won't be unloaded
					ActorReferences->Add(FWorldPartitionReference(World->GetWorldPartition(), PCGActor->GetActorGuid()));
				}

				if (!PCGActor)
				{
					return true;
				}

				TArray<FPCGTaskId> PreviousTasks;

				auto SetPreviousTaskIfValid = [&PreviousTasks](FPCGTaskId TaskId){
					if (TaskId != InvalidPCGTaskId)
					{
						PreviousTasks.Reset();
						PreviousTasks.Add(TaskId);
					}
				};

				// We'll need to make sure actors in the bounds are loaded only if we need them.
				if (bLoadCell)
				{
					auto WorldPartitionLoadActorsInBounds = [World, ActorReferences](const FWorldPartitionActorDesc* ActorDesc) {
						check(ActorDesc);
						ActorReferences->Add(FWorldPartitionReference(World->GetWorldPartition(), ActorDesc->GetGuid()));
						// Load actor if not already loaded
						ActorDesc->GetActor();
						return true;
					};

					auto LoadActorsTask = [World, IntersectedBounds, WorldPartitionLoadActorsInBounds]() {
						FWorldPartitionHelpers::ForEachIntersectingActorDesc(World->GetWorldPartition(), IntersectedBounds, WorldPartitionLoadActorsInBounds);
						return true;
					};

					FPCGTaskId LoadTaskId = GraphExecutor->ScheduleGeneric(LoadActorsTask, {});
					SetPreviousTaskIfValid(LoadTaskId);
				}

				// Execute
				FPCGTaskId ExecuteTaskId = InOperation(PCGActor, IntersectedBounds, PreviousTasks);
				SetPreviousTaskIfValid(ExecuteTaskId);

				// Save changes; note that there's no need to save if the operation was cancelled
				if (bSaveActors && ExecuteTaskId != InvalidPCGTaskId)
				{
					auto SaveActorTask = [PCGActor, GraphExecutor]() {
						GraphExecutor->AddToDirtyActors(PCGActor);
						return true;
					};

					FPCGTaskId SaveTaskId = GraphExecutor->ScheduleGeneric(SaveActorTask, PreviousTasks);
					SetPreviousTaskIfValid(SaveTaskId);
				}

				// Unload actors from cell (or the pcg actor refered here)
				auto UnloadActorsTask = [GraphExecutor, ActorReferences]() {
					GraphExecutor->AddToUnusedActors(*ActorReferences);
					return true;
				};

				// Schedule after the save (if valid), then the execute so we can queue this after the load.
				FPCGTaskId UnloadTaskId = GraphExecutor->ScheduleGeneric(UnloadActorsTask, PreviousTasks);
				SetPreviousTaskIfValid(UnloadTaskId);

				// Finally, mark "last" valid task in the cell tasks.
				CellTasks.Append(PreviousTasks);
			}

			return true;
		};

		// TODO: accumulate last phases of every cell + run a GC at the end?
		//  or add some mechanism on the graph executor side to run GC every now and then
		FActorPartitionGridHelper::ForEachIntersectingCell(APCGPartitionActor::StaticClass(), InBounds, World->PersistentLevel, CellLambda);

		// Finally, create a dummy generic task to wait on all cells
		if (!CellTasks.IsEmpty())
		{
			return GraphExecutor->ScheduleGeneric([]() { return true; }, CellTasks);
		}
		else
		{
			return InvalidPCGTaskId;
		}
	}

} // end namepsace PCGSubsystem

FPCGTaskId UPCGSubsystem::ProcessGraph(UPCGComponent* Component, const FBox& InPreviousBounds, const FBox& InNewBounds, bool bGenerate, bool bSave)
{
	check(Component && (InPreviousBounds.IsValid || InNewBounds.IsValid));
	TWeakObjectPtr<UPCGComponent> ComponentPtr(Component);

	// TODO: optimal implementation would find the difference between the previous bounds and the new bounds
	// and process these only. This is esp. important because of the CreateActor parameter.
	auto ScheduleTask = [this, ComponentPtr, InPreviousBounds, InNewBounds, bGenerate](APCGPartitionActor* PCGActor, const FBox& InBounds, const TArray<FPCGTaskId>& TaskDependencies){
		auto UnpartitionTask = [ComponentPtr, PCGActor]() {
			// TODO: PCG actors that become empty could be deleted, but we also need to keep track
			// of packages that would need to be deleted from SCC.
			if (UPCGComponent* Component = ComponentPtr.Get())
			{
				PCGActor->RemoveGraphInstance(Component);
			}
			else
			{
#if WITH_EDITOR
				PCGActor->CleanupDeadGraphInstances();
#endif
			}
			return true;
		};

		auto PartitionTask = [ComponentPtr, PCGActor]() {
			if (UPCGComponent* Component = ComponentPtr.Get())
			{
				PCGActor->AddGraphInstance(Component);
			}			
			return true;
		};

		auto ScheduleGraph = [this, PCGActor, ComponentPtr, &TaskDependencies]() {
			if (UPCGComponent* Component = ComponentPtr.Get())
			{
				// Ensure that the PCG actor has a matching local component.
				// This is done immediately, but technically we could add it as a task
				PCGActor->AddGraphInstance(Component);

				UPCGComponent* LocalComponent = PCGActor->GetLocalComponent(Component);

				if (!LocalComponent)
				{
					return InvalidPCGTaskId;
				}

				return LocalComponent->GenerateInternal(/*bForce=*/false, TaskDependencies);
			}
			else
			{
				UE_LOG(LogPCG, Error, TEXT("[ProcessGraph] PCG Component on PCG Actor is null"));
				return InvalidPCGTaskId;
			}
		};

		const bool bIsInOldBounds = InPreviousBounds.IsValid && InBounds.Intersect(InPreviousBounds);
		const bool bIsInNewBounds = InNewBounds.IsValid && InBounds.Intersect(InNewBounds);

		// 4 cases here:
		// old & new -> generate only
		// old & !new -> unpartition
		// !old & new -> partition & generate
		// !old & !new -> nothing to do
		if (bIsInOldBounds && !bIsInNewBounds)
		{
			return GraphExecutor->ScheduleGeneric(UnpartitionTask, TaskDependencies);
		}
		else if (!bIsInOldBounds && bIsInNewBounds)
		{
			// Here, we should try to schedule the local component, but it doesn't exist yet.
			// what to do? in practice, it's not a problem to not execute the Partition in a separate process,
			// since there won't be any dependencies there in this case; but, we should execute it in a task
			// if it's the only thing we do, otherwise we'll have issues with the other operations (save, etc.)
			if (bGenerate)
			{
				return ScheduleGraph();
			}
			else
			{
				return GraphExecutor->ScheduleGeneric(PartitionTask, TaskDependencies);
			}
		}
		else if (bIsInOldBounds && bIsInNewBounds && bGenerate)
		{
			return ScheduleGraph();
		}
		else
		{
			return InvalidPCGTaskId;
		}
	};

	FBox UnionBounds = InPreviousBounds + InNewBounds;
	const bool bCreateActors = (!UnionBounds.Equals(InPreviousBounds));
	const bool bLoadCell = (bGenerate && bSave); // TODO: review this
	const bool bSaveActors = (bSave);

	FPCGTaskId ProcessAllCellsTaskId = InvalidPCGTaskId;

	if (UnionBounds.IsValid)
	{
		ProcessAllCellsTaskId = PCGSubsystem::ForEachIntersectingCell(GraphExecutor, Component->GetWorld(), UnionBounds, bCreateActors, bLoadCell, bSaveActors, ScheduleTask);
	}

	// Finally, call PostProcessGraph if something happened
	if (ProcessAllCellsTaskId != InvalidPCGTaskId)
	{
		auto PostProcessGraph = [ComponentPtr, InNewBounds, bGenerate]() {
			if (UPCGComponent* Component = ComponentPtr.Get())
			{
				Component->PostProcessGraph(InNewBounds, bGenerate);
			}
			return true;
		};

		return GraphExecutor->ScheduleGeneric(PostProcessGraph, { ProcessAllCellsTaskId });
	}
	else
	{
		if(Component)
		{
			Component->OnProcessGraphAborted();
		}

		return InvalidPCGTaskId;
	}
}

void UPCGSubsystem::CleanupGraph(UPCGComponent* Component, const FBox& InBounds, bool bRemoveComponents, bool bSave)
{
	TWeakObjectPtr<UPCGComponent> ComponentPtr(Component);

	auto ScheduleTask = [this, ComponentPtr, bRemoveComponents](APCGPartitionActor* PCGActor, const FBox& InIntersectedBounds, const TArray<FPCGTaskId>& TaskDependencies) {
		auto CleanupTask = [ComponentPtr, PCGActor, bRemoveComponents]() {
			check(ComponentPtr.Get() != nullptr && PCGActor != nullptr);
			UPCGComponent* Component = ComponentPtr.Get();

			if (!PCGActor || !Component)
			{
				return true;
			}

			if (UPCGComponent* LocalComponent = PCGActor->GetLocalComponent(Component))
			{
				LocalComponent->CleanupInternal(bRemoveComponents);
			}

			return true;
		};

		return GraphExecutor->ScheduleGeneric(CleanupTask, TaskDependencies);
	};

	PCGSubsystem::ForEachIntersectingCell(GraphExecutor, Component->GetWorld(), InBounds, /*bCreateActor=*/false, /*bLoadCell=*/false, bSave, ScheduleTask);
}

void UPCGSubsystem::DirtyGraph(UPCGComponent* Component, const FBox& InBounds, EPCGComponentDirtyFlag DirtyFlag)
{
	TWeakObjectPtr<UPCGComponent> ComponentPtr(Component);

	auto ScheduleTask = [this, ComponentPtr, DirtyFlag](APCGPartitionActor* PCGActor, const FBox& InIntersectedBounds, const TArray<FPCGTaskId>& TaskDependencies) {
		// In the specific case of the dirty, we want to bypass the execution queue, esp. since there's nothing happening here
		// so we will run the command now, and not delay it.
		check(ComponentPtr.Get() != nullptr && PCGActor != nullptr);
		UPCGComponent* Component = ComponentPtr.Get();

		if (!PCGActor || !Component)
		{
			return InvalidPCGTaskId;
		}

		if (UPCGComponent* LocalComponent = PCGActor->GetLocalComponent(Component))
		{
			LocalComponent->DirtyGenerated(DirtyFlag);
		}

		return InvalidPCGTaskId;
	};

	// TODO: Could implement a "ForEachLoadedIntersectingCellImmediate" method to clarify this use case
	PCGSubsystem::ForEachIntersectingCell(GraphExecutor, Component->GetWorld(), InBounds, /*bCreateActor=*/false, /*bLoadCell=*/false, /*bSave=*/false, ScheduleTask);
}

void UPCGSubsystem::CleanupPartitionActors(const FBox& InBounds)
{
	auto ScheduleTask = [this](APCGPartitionActor* PCGActor, const FBox& InIntersectedBounds, const TArray<FPCGTaskId>& TaskDependencies) {
		auto CleanupTask = [PCGActor]() {
			check(PCGActor);
			PCGActor->CleanupDeadGraphInstances();

			return true;
		};

		return GraphExecutor->ScheduleGeneric(CleanupTask, TaskDependencies);
	};

	PCGSubsystem::ForEachIntersectingCell(GraphExecutor, GetWorld(), InBounds, /*bCreateActor=*/false, /*bLoadCell=*/false, /*bSave=*/false, ScheduleTask);
}

void UPCGSubsystem::ClearPCGLink(UPCGComponent* InComponent, const FBox& InBounds, AActor* InNewActor)
{
	TWeakObjectPtr<AActor> NewActorPtr(InNewActor);
	TWeakObjectPtr<UPCGComponent> ComponentPtr(InComponent);

	auto ScheduleTask = [this, NewActorPtr, ComponentPtr](APCGPartitionActor* PCGActor, const FBox& InIntersectedBounds, const TArray<FPCGTaskId>& TaskDependencies) {
		auto MoveTask = [this, NewActorPtr, ComponentPtr, PCGActor]() {
			check(NewActorPtr.IsValid() && ComponentPtr.IsValid() && PCGActor != nullptr);

			if (TObjectPtr<UPCGComponent> LocalComponent = PCGActor->GetLocalComponent(ComponentPtr.Get()))
			{
				LocalComponent->MoveResourcesToNewActor(NewActorPtr.Get(), /*bCreateChild=*/true);
			}

			return true;
		};

		return GraphExecutor->ScheduleGeneric(MoveTask, TaskDependencies);
	};

	FPCGTaskId TaskId = PCGSubsystem::ForEachIntersectingCell(GraphExecutor, GetWorld(), InBounds, /*bCreateActor=*/false, /*bLoadCell=*/false, /*bSave=*/false, ScheduleTask);

	// Verify if the NewActor has some components attached to its root or attached actors. If not, destroy it.
	// Return false if the new actor is not valid or destroyed.
	auto VerifyAndDestroyNewActor = [this, NewActorPtr]() {
		check(NewActorPtr.IsValid());

		USceneComponent* RootComponent = NewActorPtr->GetRootComponent();
		check(RootComponent);

		AActor* NewActor = NewActorPtr.Get();

		TArray<AActor*> AttachedActors;
		NewActor->GetAttachedActors(AttachedActors);

		if (RootComponent->GetNumChildrenComponents() == 0 && AttachedActors.IsEmpty())
		{
			GetWorld()->DestroyActor(NewActor);
			return false;
		}

		return true;
	};

	if (TaskId != InvalidPCGTaskId)
	{
		auto CleanupTask = [this, ComponentPtr, VerifyAndDestroyNewActor]() {

			// If the new actor is valid, clean up the original component.
			if (VerifyAndDestroyNewActor())
			{
				check(ComponentPtr.IsValid());
				ComponentPtr->Cleanup(/*bRemoveComponents=*/true);
			}

			return true;
		};

		GraphExecutor->ScheduleGeneric(CleanupTask, { TaskId });
	}
	else
	{
		VerifyAndDestroyNewActor();
	}
}

void UPCGSubsystem::DeletePartitionActors()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGSubsystem::DeletePartitionActors);

	// TODO: This should be handled differently when we are able to stop a generation
	// For now, we need to be careful and not delete partition actors that are linked to components that are currently
	// generating stuff.
	// Also, we keep a set on all those actors, in case the partition actor status changes during the loop.
	TSet<TObjectPtr<APCGPartitionActor>> ActorsNotSafeToBeDeleted;

	TSet<UPackage*> PackagesToCleanup;
	TSet<FString> PackagesToDeleteFromSCC;
	UWorld* World = GetWorld();

	if (!World || !World->GetWorldPartition())
	{
		return;
	}

	auto GatherAndDestroyLoadedActors = [&PackagesToCleanup, &PackagesToDeleteFromSCC, World, &ActorsNotSafeToBeDeleted](AActor* Actor) -> bool
	{
		// Make sure that this actor was not flagged to not be deleted, or not safe for deletion
		TObjectPtr<APCGPartitionActor> PartitionActor = CastChecked<APCGPartitionActor>(Actor);
		if (!PartitionActor->IsSafeForDeletion())
		{
			ActorsNotSafeToBeDeleted.Add(PartitionActor);
		}
		else if (!ActorsNotSafeToBeDeleted.Contains(PartitionActor))
		{
			// Also reset the last generated bounds to indicate to the component to re-create its PartitionActors when it generates.
			for (TObjectPtr<UPCGComponent> PCGComponent : PartitionActor->GetAllOriginalPCGComponents())
			{
				if (PCGComponent)
				{
					PCGComponent->ResetLastGeneratedBounds();
				}
			}

			if (UPackage* ExternalPackage = PartitionActor->GetExternalPackage())
			{
				PackagesToCleanup.Add(ExternalPackage);
			}

			World->DestroyActor(PartitionActor);
		}

		return true;
	};

	auto GatherAndDestroyActors = [&PackagesToCleanup, &PackagesToDeleteFromSCC, World, &ActorsNotSafeToBeDeleted, &GatherAndDestroyLoadedActors](const FWorldPartitionActorDesc* ActorDesc) {
		if (ActorDesc->GetActor())
		{
			GatherAndDestroyLoadedActors(ActorDesc->GetActor());
		}
		else
		{
			PackagesToDeleteFromSCC.Add(ActorDesc->GetActorPackage().ToString());
			World->GetWorldPartition()->RemoveActor(ActorDesc->GetGuid());
		}

		return true;
	};

	// First, clear selection otherwise it might crash
	if (GEditor)
	{
		GEditor->SelectNone(true, true, false);
	}

	FWorldPartitionHelpers::ForEachActorDesc<APCGPartitionActor>(World->GetWorldPartition(), GatherAndDestroyActors);

	// Also cleanup the remaining actors that don't have descriptors, if we have a loaded level
	if (ULevel* Level = World->GetCurrentLevel())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPCGSubsystem::DeletePartitionActors::ForEachActorInLevel);
		UPCGActorHelpers::ForEachActorInLevel<APCGPartitionActor>(Level, GatherAndDestroyLoadedActors);
	}

	if (!ActorsNotSafeToBeDeleted.IsEmpty())
	{
		// FIXME: see at the top for this function.
		UE_LOG(LogPCG, Error, TEXT("Tried to delete PCGPartitionActors while their PCGComponent were refreshing. All PCGPartitionActors that are linked to those PCGComponents won't be deleted. You should retry deleting them when the refresh is done."));
	}

	if (PackagesToCleanup.Num() > 0)
	{
		ObjectTools::CleanupAfterSuccessfulDelete(PackagesToCleanup.Array(), /*bPerformanceReferenceCheck=*/true);
	}

	if (PackagesToDeleteFromSCC.Num() > 0)
	{
		FPackageSourceControlHelper PackageHelper;
		if (!PackageHelper.Delete(PackagesToDeleteFromSCC.Array()))
		{
			// Log error...
		}
	}
}

void UPCGSubsystem::NotifyGraphChanged(UPCGGraph* InGraph)
{
	if (GraphExecutor)
	{
		GraphExecutor->NotifyGraphChanged(InGraph);
	}
}

void UPCGSubsystem::CleanFromCache(const IPCGElement* InElement)
{
	if (GraphExecutor)
	{
		GraphExecutor->GetCache().CleanFromCache(InElement);
	}
}

void UPCGSubsystem::FlushCache()
{
	if (GraphExecutor)
	{
		GraphExecutor->GetCache().ClearCache();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
void UPCGSubsystem::DelayPartitionGraph(UPCGComponent* Component)
{
	DelayProcessGraph(Component, /*bGenerate=*/false, /*bSave=*/false, /*bUseEmptyNewBounds=*/false);
}

void UPCGSubsystem::DelayUnpartitionGraph(UPCGComponent* Component)
{
	DelayProcessGraph(Component, /*bGenerate=*/false, /*bSave=*/false, /*bUseEmptyNewBounds=*/true);
}

FPCGTaskId UPCGSubsystem::DelayGenerateGraph(UPCGComponent* Component, bool bSave)
{
	return DelayProcessGraph(Component, /*bGenerate=*/true, bSave, /*bUseEmptyNewBounds=*/false);
}

FPCGTaskId UPCGSubsystem::DelayProcessGraph(UPCGComponent* Component, bool bGenerate, bool bSave, bool bUseEmptyNewBounds)
{
	check(Component && Component->IsPartitioned());
	TWeakObjectPtr<UPCGComponent> ComponentPtr(Component);

	auto ExecuteProcessGraph = [this, ComponentPtr, bGenerate, bSave, bUseEmptyNewBounds]() {
		if (UPCGComponent* Component = ComponentPtr.Get())
		{
			const FBox PreviousBounds = Component->LastGeneratedBounds;
			FBox NewBounds = bUseEmptyNewBounds ? FBox(EForceInit::ForceInit) : Component->GetGridBounds();
			/*FPCGTaskId GraphTaskId = */ ProcessGraph(Component, PreviousBounds, NewBounds, bGenerate, bSave);
		}

		return true;
	};

	// Delayed graph scheduling
	return GraphExecutor->ScheduleGeneric(ExecuteProcessGraph, {});
}

#endif // WITH_EDITOR
