// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionStreamingGeneration.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ActorReferencesUtils.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionActorCluster.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerUtils.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationNullErrorHandler.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationLogErrorHandler.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationMapCheckErrorHandler.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "WorldPartition"

static FAutoConsoleCommand DumpStreamingGenerationLog(
	TEXT("wp.Editor.DumpStreamingGenerationLog"),
	TEXT("Dump the streaming generation log."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World(); World && !World->IsGameWorld())
		{
			if (UWorldPartition* WorldPartition = World->GetWorldPartition())
			{
				WorldPartition->GenerateStreaming();
				WorldPartition->FlushStreaming();
			}
		}
	})
);

FActorDescViewMap::FActorDescViewMap()
{}

TArray<const FWorldPartitionActorDescView*> FActorDescViewMap::FindByExactNativeClass(UClass* InExactNativeClass) const
{
	check(InExactNativeClass->IsNative());
	const FName NativeClassName = InExactNativeClass->GetFName();
	TArray<const FWorldPartitionActorDescView*> Result;
	ActorDescViewsByClass.MultiFind(NativeClassName, Result);
	return Result;
}

FWorldPartitionActorDescView* FActorDescViewMap::Emplace(const FGuid& InGuid, const FWorldPartitionActorDescView& InActorDescView)
{
	FWorldPartitionActorDescView* NewActorDescView = ActorDescViewList.Emplace_GetRef(MakeUnique<FWorldPartitionActorDescView>(InActorDescView)).Get();
	
	const UClass* NativeClass = NewActorDescView->GetActorNativeClass();
	const FName NativeClassName = NativeClass->GetFName();

	ActorDescViewsByGuid.Emplace(InGuid, NewActorDescView);
	ActorDescViewsByClass.Add(NativeClassName, NewActorDescView);

	return NewActorDescView;
}

/*
	Preparation Phase
		Actor Descriptor Views Creation
		Actor Descriptor Views Validation
		Actor Clusters Creation

	Generation Phase
		Streaming Grids Generation
		Data Layers Split Pass

	Output Phase
		Report Generation
		SubLevels Generation
		HLOD Generation
*/

class FWorldPartitionStreamingGenerator
{
	void ResolveRuntimeDataLayers(FWorldPartitionActorDescView& ActorDescView, const FActorDescViewMap& ActorDescViewMap)
	{
		TArray<FName> RuntimeDataLayerInstanceNames;
		RuntimeDataLayerInstanceNames.Reserve(ActorDescView.GetDataLayers().Num());

		if (FDataLayerUtils::ResolveRuntimeDataLayerInstanceNames(ActorDescView, ActorDescViewMap, RuntimeDataLayerInstanceNames))
		{
			ActorDescView.SetRuntimeDataLayers(RuntimeDataLayerInstanceNames);
		}
	}

	void ResolveRuntimeReferences(FWorldPartitionActorDescView& ActorDescView, const FActorDescViewMap& ActorDescViewMap)
	{
		TArray<FGuid> RuntimeReferences;
		RuntimeReferences.Reserve(ActorDescView.GetReferences().Num());

		for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
		{
			if (const FWorldPartitionActorDescView* ReferenceDescView = ActorDescViewMap.FindByGuid(ReferenceGuid))
			{
				check(!ReferenceDescView->GetActorIsEditorOnly());
				RuntimeReferences.Add(ReferenceGuid);
			}
		}

		if (RuntimeReferences.Num() != ActorDescView.GetReferences().Num())
		{
			ActorDescView.SetRuntimeReferences(RuntimeReferences);
		}
	}

	void CreateActorDescViewMap(const UActorDescContainer* InContainer, FActorDescViewMap& OutActorDescViewMap, const FActorContainerID& InContainerID, TArray<FWorldPartitionActorDescView>& OutContainerInstances)
	{
		// should we handle unsaved or newly created actors?
		const bool bHandleUnsavedActors = ModifiedActorsDescList && InContainerID.IsMainContainer();

		// Consider all actors of a /Temp/ container package as Unsaved because loading them from disk will fail (Outer world name mismatch)
		const bool bIsTempContainerPackage = FPackageName::IsTempPackage(InContainer->GetPackage()->GetName());
		
		// Test whether an actor is editor only. Will fallback to the actor descriptor only if the actor is not loaded
		auto IsActorEditorOnly = [](const FWorldPartitionActorDesc* ActorDesc, const FActorContainerID& ContainerID) -> bool
		{
			if (ActorDesc->IsRuntimeRelevant(ContainerID))
			{
				if (ActorDesc->IsLoaded())
				{
					return ActorDesc->GetActor()->IsEditorOnly();
				}
				else
				{
					return ActorDesc->GetActorIsEditorOnly();
				}
			}
			return true;
		};

		// Create an actor descriptor view for the specified actor (modified or unsaved actors)
		auto GetModifiedActorDesc = [this](AActor* InActor, const UActorDescContainer* InContainer) -> FWorldPartitionActorDesc*
		{
			FWorldPartitionActorDesc* ModifiedActorDesc = ModifiedActorsDescList->AddActor(InActor);

			// Pretend that this actor descriptor belongs to the original container, even if it's not present. It's essentially a proxy
			// descriptor on top an existing one and at this point no code should require to access the container to resolve it anyways.
			ModifiedActorDesc->SetContainer(const_cast<UActorDescContainer*>(InContainer));

			return ModifiedActorDesc;
		};

		// Register the actor descriptor view
		auto RegisterActorDescView = [this, InContainer, &OutActorDescViewMap, &OutContainerInstances](const FGuid& ActorGuid, FWorldPartitionActorDescView& ActorDescView)
		{
			if (ActorDescView.IsContainerInstance())
			{
				OutContainerInstances.Add(ActorDescView);
			}
			else
			{
				OutActorDescViewMap.Emplace(ActorGuid, ActorDescView);
			}
		};
		
		TMap<FGuid, FGuid> ContainerGuidsRemap;
		for (FActorDescList::TConstIterator<> ActorDescIt(InContainer); ActorDescIt; ++ActorDescIt)
		{
			if (!IsActorEditorOnly(*ActorDescIt, InContainerID))
			{
				// Handle unsaved actors
				if (AActor* Actor = ActorDescIt->GetActor())
				{
					// Deleted actors
					if (!IsValid(Actor))
					{
						continue;
					}

					// Dirty actors
					if (bHandleUnsavedActors && (bIsTempContainerPackage || Actor->GetPackage()->IsDirty()))
					{
						// Dirty, unsaved actor for PIE
						FWorldPartitionActorDescView ModifiedActorDescView = GetModifiedActorDesc(Actor, InContainer);
						RegisterActorDescView(ActorDescIt->GetGuid(), ModifiedActorDescView);
						continue;
					}
				}

				// Non-dirty actor
				FWorldPartitionActorDescView ActorDescView(*ActorDescIt);
				RegisterActorDescView(ActorDescIt->GetGuid(), ActorDescView);
			}
		}

		// Append new unsaved actors for the persistent level
		if (bHandleUnsavedActors)
		{
			for (AActor* Actor : InContainer->GetWorld()->PersistentLevel->Actors)
			{
				if (IsValid(Actor) && Actor->IsPackageExternal() && Actor->IsMainPackageActor() && !Actor->IsEditorOnly() && !InContainer->GetActorDesc(Actor->GetActorGuid()))
				{
					FWorldPartitionActorDescView ModifiedActorDescView = GetModifiedActorDesc(Actor, InContainer);
					RegisterActorDescView(Actor->GetActorGuid(), ModifiedActorDescView);
				}
			}
		}
	}

	void CreateActorDescriptorViewsRecursive(const UActorDescContainer* InContainer, const FTransform& InTransform, const TSet<FName>& InRuntimeDataLayers, const FActorContainerID& InContainerID, const FActorContainerID& InParentContainerID, EContainerClusterMode InClusterMode, const TCHAR* OwnerName)
	{
		FActorDescViewMap ActorDescViewMap;
		TArray<FWorldPartitionActorDescView> ContainerInstanceViews;
		
		// Gather actor descriptor views for this container
		CreateActorDescViewMap(InContainer, ActorDescViewMap, InContainerID, ContainerInstanceViews);

		// Parse actor containers
		for (const FWorldPartitionActorDescView& ContainerInstanceView : ContainerInstanceViews)
		{
			const UActorDescContainer* SubContainer;
			EContainerClusterMode SubClusterMode;
			FTransform SubTransform;

			if (!ContainerInstanceView.GetContainerInstance(SubContainer, SubTransform, SubClusterMode))
			{
				//@todo_ow: make a specific error for missing container instance sublevel?
				ErrorHandler->OnInvalidReference(ContainerInstanceView, FGuid());
				continue;
			}

			check(SubContainer);

			const FGuid ActorGuid = ContainerInstanceView.GetGuid();
			const FActorContainerID SubContainerID(InContainerID, ActorGuid);

			// Combine actor runtime Data Layers with parent container runtime Data Layers
			TSet<FName> CombinedRuntimeDataLayers = InRuntimeDataLayers;
			CombinedRuntimeDataLayers.Append(ContainerInstanceView.GetRuntimeDataLayers());

			CreateActorDescriptorViewsRecursive(SubContainer, SubTransform * InTransform, CombinedRuntimeDataLayers, SubContainerID, InContainerID, SubClusterMode, *ContainerInstanceView.GetActorLabelOrName().ToString());
		}

		// Create container descriptor
		check(!ContainerDescriptorsMap.Contains(InContainerID));

		FContainerDescriptor& ContainerDescriptor = ContainerDescriptorsMap.Add(InContainerID);
		ContainerDescriptor.Container = InContainer;
		ContainerDescriptor.Transform = InTransform;
		ContainerDescriptor.ClusterMode = InClusterMode;
		ContainerDescriptor.ActorDescViewMap = MoveTemp(ActorDescViewMap);
		ContainerDescriptor.RuntimeDataLayers = InRuntimeDataLayers;
		ContainerDescriptor.OwnerName = OwnerName;

		// Maintain containers hierarchy, bottom up
		if (InContainerID != InParentContainerID)
		{
			ContainersHierarchy.Add(InContainerID, InParentContainerID);
		}
	}

	/** 
	 * Creates the actor descriptor views for the specified container.
	 */
	void CreateActorDescriptorViews(const UActorDescContainer* InContainer)
	{
		CreateActorDescriptorViewsRecursive(InContainer, FTransform::Identity, TSet<FName>(), FActorContainerID(), FActorContainerID(), EContainerClusterMode::Partitioned, TEXT("MainContainer"));

		// Resolve actor descriptor views once all views are created
		for (auto It = ContainerDescriptorsMap.CreateIterator(); It; ++It)
		{
			const FActorContainerID& ContainerID = It.Key();
			FContainerDescriptor& ContainerDescriptor = It.Value();

			ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([this, &ContainerDescriptor](FWorldPartitionActorDescView& ActorDescView)
			{
				if (!bEnableStreaming)
				{
					ActorDescView.SetForcedNonSpatiallyLoaded();
				}

				ResolveRuntimeDataLayers(ActorDescView, ContainerDescriptor.ActorDescViewMap);
				ResolveRuntimeReferences(ActorDescView, ContainerDescriptor.ActorDescViewMap);
			});
		}
	}

	/** 
	 * Perform various validations on actor descriptor views, and adjust them based on different requirements. This needs to happen before updating containers bounds because
	 * Some actor descriptor views might change grid placement, etc.
	 */
	void ValidateActorDescriptorViews()
	{
		for (auto It = ContainerDescriptorsMap.CreateIterator(); It; ++It)
		{
			const FActorContainerID& ContainerID = It.Key();
			FContainerDescriptor& ContainerDescriptor = It.Value();

			if (ContainerID.IsMainContainer() && ContainerDescriptor.Container->GetWorld())
			{
				// Gather all references to external actors from the level script and make them always loaded
				if (ULevelScriptBlueprint* LevelScriptBlueprint = ContainerDescriptor.Container->GetWorld()->PersistentLevel->GetLevelScriptBlueprint(true))
				{
					TArray<AActor*> LevelScriptExternalActorReferences = ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint);

					for (AActor* Actor : LevelScriptExternalActorReferences)
					{
						if (FWorldPartitionActorDescView* ActorDescView = ContainerDescriptor.ActorDescViewMap.FindByGuid(Actor->GetActorGuid()))
						{
							if (ActorDescView->GetIsSpatiallyLoaded())
							{
								ErrorHandler->OnInvalidReferenceLevelScriptStreamed(*ActorDescView);
								ActorDescView->SetForcedNonSpatiallyLoaded();
							}

							if (ActorDescView->GetRuntimeDataLayers().Num())
							{
								ErrorHandler->OnInvalidReferenceLevelScriptDataLayers(*ActorDescView);
								ActorDescView->SetInvalidDataLayers();
							}
						}
					}
				}
			}

			// Perform various adjustements based on validations and report errors
			//
			// The first validation pass is used to report errors, subsequent passes are used to make corrections to the FWorldPartitionActorDescView
			// Since the references can form cycles/long chains in the data fixes might need to be propagated in multiple passes.
			// 
			// This works because fixes are deterministic and always apply the same way to both Actors being modified, so there's no ordering issues possible
			int32 NbErrorsDetected = INDEX_NONE;
			for(uint32 NbValidationPasses = 0; NbErrorsDetected; NbValidationPasses++)
			{
				NbErrorsDetected = 0;

				ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([this, &ContainerDescriptor, &NbErrorsDetected, &NbValidationPasses](FWorldPartitionActorDescView& ActorDescView)
				{
					// Validate data layers
					auto IsReferenceGridPlacementValid = [](const FWorldPartitionActorDescView& RefererActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
					{
						const bool bIsActorDescSpatiallyLoaded = RefererActorDescView.GetIsSpatiallyLoaded();
						const bool bIsActorDescRefSpatiallyLoaded = ReferenceActorDescView.GetIsSpatiallyLoaded();

						// The only case we support right now is spatially loaded actors referencing non-spatially loaded actors, when target is not in data layers.
						// For this to work with data layers, we need to implement dependency logic support in the content cooker splitter.
						if (bIsActorDescSpatiallyLoaded && !bIsActorDescRefSpatiallyLoaded && ReferenceActorDescView.GetDataLayers().IsEmpty())
						{
							return true;
						}

						return bIsActorDescSpatiallyLoaded == bIsActorDescRefSpatiallyLoaded;
					};

					// Validate grid placement
					auto IsReferenceDataLayersValid = [](const FWorldPartitionActorDescView& RefererActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
					{
						if (RefererActorDescView.GetRuntimeDataLayers().Num() == ReferenceActorDescView.GetRuntimeDataLayers().Num())
						{
							const TSet<FName> RefererActorDescDataLayers(RefererActorDescView.GetRuntimeDataLayers());
							const TSet<FName> ReferenceActorDescDataLayers(ReferenceActorDescView.GetRuntimeDataLayers());

							return RefererActorDescDataLayers.Includes(ReferenceActorDescDataLayers);
						}

						return false;
					};

					// Validate runtime grid
					auto IsReferenceRuntimeGridValid = [](const FWorldPartitionActorDescView& RefererActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
					{
						return RefererActorDescView.GetRuntimeGrid() == ReferenceActorDescView.GetRuntimeGrid();
					};

					struct FActorReferenceInfo
					{
						FGuid ActorGuid;
						FWorldPartitionActorDescView* ActorDesc;
						FGuid ReferenceGuid;
						FWorldPartitionActorDescView* ReferenceActorDesc;
					};

					// Build references List
					TArray<FActorReferenceInfo> References;

					// Add normal actor references
					for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
					{
						if (ReferenceGuid != ActorDescView.GetParentActor()) // References to the parent are inversed in their handling 
						{
							// Filter out parent back references
							FWorldPartitionActorDescView* ReferenceActorDesc = ContainerDescriptor.ActorDescViewMap.FindByGuid(ReferenceGuid);
							if (ReferenceActorDesc && ReferenceActorDesc->GetParentActor() == ActorDescView.GetGuid())
							{
								continue;
							}

							References.Emplace(FActorReferenceInfo{ ActorDescView.GetGuid(), &ActorDescView, ReferenceGuid, ReferenceActorDesc });
						}
					}

					// Add attach reference for the topmost parent, this reference is inverted since we consider the top most existing 
					// parent to be refering to us, not the child to be referering the parent
					FGuid ParentGuid = ActorDescView.GetParentActor();
					FWorldPartitionActorDescView* TopParentDescView = nullptr;

					while (ParentGuid.IsValid())
					{
						FWorldPartitionActorDescView* ParentDescView = ContainerDescriptor.ActorDescViewMap.FindByGuid(ParentGuid);
					
						if (ParentDescView)
						{
							TopParentDescView = ParentDescView;
							ParentGuid = ParentDescView->GetParentActor();
						}
						else
						{
							// we had a guid but parent cannot be found, this will be a missing reference
							break; 
						}
					}

					if (TopParentDescView)
					{
						References.Emplace(FActorReferenceInfo{ TopParentDescView->GetGuid(), TopParentDescView, ActorDescView.GetGuid(), &ActorDescView });
					}

					if (ParentGuid.IsValid())
					{
						// In case of missing parent add a missing reference 
						References.Emplace(FActorReferenceInfo{ ActorDescView.GetGuid(), &ActorDescView, ParentGuid, nullptr });
					}

					for (FActorReferenceInfo& Info : References)
					{
						FWorldPartitionActorDescView* RefererActorDescView = Info.ActorDesc;
						FWorldPartitionActorDescView* ReferenceActorDescView = Info.ReferenceActorDesc;

						if (ReferenceActorDescView)
						{
							// Validate grid placement
							if (!IsReferenceGridPlacementValid(*RefererActorDescView, *ReferenceActorDescView))
							{
								if (!NbValidationPasses)
								{
									ErrorHandler->OnInvalidReferenceGridPlacement(*RefererActorDescView, *ReferenceActorDescView);									
								}
								else
								{
									RefererActorDescView->SetForcedNonSpatiallyLoaded();
									ReferenceActorDescView->SetForcedNonSpatiallyLoaded();
								}

								NbErrorsDetected++;
							}

							if (!IsReferenceDataLayersValid(*RefererActorDescView, *ReferenceActorDescView))
							{
								if (!NbValidationPasses)
								{
									ErrorHandler->OnInvalidReferenceDataLayers(*RefererActorDescView, *ReferenceActorDescView);									
								}
								else
								{
									RefererActorDescView->SetInvalidDataLayers();
									ReferenceActorDescView->SetInvalidDataLayers();
								}

								NbErrorsDetected++;
							}

							if (!IsReferenceRuntimeGridValid(*RefererActorDescView, *ReferenceActorDescView))
							{
								if (!NbValidationPasses)
								{
									ErrorHandler->OnInvalidReferenceRuntimeGrid(*RefererActorDescView, *ReferenceActorDescView);
								}
								else
								{
									RefererActorDescView->SetInvalidRuntimeGrid();
									ReferenceActorDescView->SetInvalidRuntimeGrid();
								}

								NbErrorsDetected++;
							}

						}
						else
						{
							if (!NbValidationPasses)
							{
								ErrorHandler->OnInvalidReference(*RefererActorDescView, Info.ReferenceGuid);
							}
							// Do not increment NbErrorsDetected since it won't be fixed and thus will always occur
						}
					}
				});		
			}

			// Report actors that need to be resaved
			ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([this](FWorldPartitionActorDescView& ActorDescView)
			{
				if (ActorDescView.IsResaveNeeded())
				{
					ErrorHandler->OnActorNeedsResave(ActorDescView);
				}
			});

			// Validate data layers
			if (ContainerID.IsMainContainer() && ContainerDescriptor.Container->GetWorld())
			{
				if (UDataLayerSubsystem* DataLayerSubsystem = UWorld::GetSubsystem<UDataLayerSubsystem>(ContainerDescriptor.Container->GetWorld()))
				{
					DataLayerSubsystem->ForEachDataLayer([this](const UDataLayerInstance* DataLayerInstance)
					{
						DataLayerInstance->Validate(ErrorHandler);
						return true;
					});
					break;
				}
			}
		}
	}

	/** 
	 * Update the actor descriptor containers to adjust their bounds from actor descriptor views.
	 */
	void UpdateContainerDescriptors()
	{
		// Update containers bounds
		for (auto ContainerIt = ContainerDescriptorsMap.CreateIterator(); ContainerIt; ++ContainerIt)
		{
			const FActorContainerID& ContainerID = ContainerIt.Key();
			FContainerDescriptor& ContainerDescriptor = ContainerIt.Value();

			ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([&ContainerDescriptor](const FWorldPartitionActorDescView& ActorDescView)
			{
				if (ActorDescView.GetIsSpatiallyLoaded())
				{
					ContainerDescriptor.Bounds += ActorDescView.GetBounds().TransformBy(ContainerDescriptor.Transform);
				}
			});
		}

		// Update parent containers bounds, this relies on the fact that ContainersHierarchy is built bottom up
		for (auto ContainerPairIt = ContainersHierarchy.CreateIterator(); ContainerPairIt; ++ContainerPairIt)
		{
			const FContainerDescriptor& CurrentContainer = ContainerDescriptorsMap.FindChecked(ContainerPairIt.Key());
			FContainerDescriptor& ParentContainer = ContainerDescriptorsMap.FindChecked(ContainerPairIt.Value());
			ParentContainer.Bounds += CurrentContainer.Bounds;
		}
	}

public:
	FWorldPartitionStreamingGenerator(FActorDescList* InModifiedActorsDescList, IStreamingGenerationErrorHandler* InErrorHandler, bool bInEnableStreaming)
	: bEnableStreaming(bInEnableStreaming)
	, ModifiedActorsDescList(InModifiedActorsDescList)
	, ErrorHandler(InErrorHandler ? InErrorHandler : &NullErrorHandler)	
	{}

	void PreparationPhase(const UActorDescContainer* Container)
	{
		// Preparation Phase :: Actor Descriptor Views Creation
		CreateActorDescriptorViews(Container);

		// Preparation Phase :: Actor Descriptor Views Validation
		ValidateActorDescriptorViews();

		// Update container descriptors
		UpdateContainerDescriptors();
	}

	FActorClusterContext CreateActorClusters(FActorClusterContext::FFilterActorDescViewFunc InFilterActorDescViewFunc = nullptr)
	{
		TArray<FActorContainerInstance> ContainerInstances;
		ContainerInstances.Reserve(ContainerDescriptorsMap.Num());

		for (auto It = ContainerDescriptorsMap.CreateConstIterator(); It; ++It)
		{
			const FActorContainerID& ContainerID = It.Key();
			const FContainerDescriptor& ContainerDescriptor = It.Value();

			ContainerInstances.Emplace(ContainerID, ContainerDescriptor.Transform, ContainerDescriptor.Bounds, ContainerDescriptor.RuntimeDataLayers, ContainerDescriptor.ClusterMode, ContainerDescriptor.Container, ContainerDescriptor.ActorDescViewMap.ActorDescViewsByGuid);
		}

		return FActorClusterContext(MoveTemp(ContainerInstances), InFilterActorDescViewFunc);
	}

	static TUniquePtr<FArchive> CreateDumpStateLogArchive(const TCHAR* Suffix)
	{
		const FString StateLogOutputFilename = FPaths::ProjectSavedDir() / TEXT("WorldPartition") / FString::Printf(TEXT("StreamingGeneration-%s-%08x-%s.log"), Suffix, FPlatformProcess::GetCurrentProcessId(), *FDateTime::Now().ToString());
		return TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*StateLogOutputFilename));
	}

	void DumpStateLog(FHierarchicalLogArchive& Ar)
	{
		// Build the containers tree representation
		TMultiMap<FActorContainerID, FActorContainerID> InvertedContainersHierarchy;
		for (auto ContainerPairIt = ContainersHierarchy.CreateIterator(); ContainerPairIt; ++ContainerPairIt)
		{
			const FActorContainerID& ChildContainerID = ContainerPairIt.Key();
			const FActorContainerID& ParentContainerID = ContainerPairIt.Value();				
			InvertedContainersHierarchy.Add(ParentContainerID, ChildContainerID);
		}

		Ar.Printf(TEXT("Containers:"));

		auto DumpContainers = [this, &InvertedContainersHierarchy, &Ar](const FActorContainerID& ContainerID)
		{
			auto DumpContainersRecursive = [this, &InvertedContainersHierarchy, &Ar](const FActorContainerID& ContainerID, auto& RecursiveFunc) -> void
			{
				const FContainerDescriptor& ContainerDescriptor = ContainerDescriptorsMap.FindChecked(ContainerID);
				
				{
					FHierarchicalLogArchive::FIndentScope IndentScope = Ar.PrintfIndent(TEXT("%s:"), *ContainerDescriptor.OwnerName);

					Ar.Printf(TEXT("       ID: 0x%016llx"), ContainerID.ID);
					Ar.Printf(TEXT("   Bounds: %s"), *ContainerDescriptor.Bounds.ToString());
					Ar.Printf(TEXT("Transform: %s"), *ContainerDescriptor.Transform.ToString());
					Ar.Printf(TEXT("Container: %s"), *ContainerDescriptor.Container->GetContainerPackage().ToString());
				}

				if (ContainerDescriptor.ActorDescViewMap.ActorDescViewsByGuid.Num())
				{
					FHierarchicalLogArchive::FIndentScope IndentScope = Ar.PrintfIndent(TEXT("ActorDescs:"));

					TMap<FGuid, FWorldPartitionActorDescView*> SortedActorDescViewMap = ContainerDescriptor.ActorDescViewMap.ActorDescViewsByGuid;
					SortedActorDescViewMap.KeySort([](const FGuid& GuidA, const FGuid& GuidB) { return GuidA < GuidB; });

					for (auto ActorDescIt = SortedActorDescViewMap.CreateConstIterator(); ActorDescIt; ++ActorDescIt)
					{
						const FWorldPartitionActorDescView& ActorDescView = *ActorDescIt.Value();
						Ar.Print(*ActorDescView.ToString());
					}
				}

				TArray<FActorContainerID> ChildContainersIDs;
				InvertedContainersHierarchy.MultiFind(ContainerID, ChildContainersIDs);
				ChildContainersIDs.Sort([](const FActorContainerID& ActorContainerIDA, const FActorContainerID& ActorContainerIDB) { return ActorContainerIDA.ID < ActorContainerIDB.ID; });

				if (ChildContainersIDs.Num())
				{
					FHierarchicalLogArchive::FIndentScope IndentScope = Ar.PrintfIndent(TEXT("SubContainers:"));
						
					for (const FActorContainerID& ChildContainerID : ChildContainersIDs)
					{
						RecursiveFunc(ChildContainerID, RecursiveFunc);
					}
				}
			};

			DumpContainersRecursive(ContainerID, DumpContainersRecursive);
		};

		DumpContainers(FActorContainerID());
	}

private:
	bool bEnableStreaming;
	FActorDescList* ModifiedActorsDescList;
	IStreamingGenerationErrorHandler* ErrorHandler;
	FStreamingGenerationNullErrorHandler NullErrorHandler;

	struct FContainerDescriptor
	{
		FContainerDescriptor()
		: Bounds(ForceInit)
		, Container(nullptr)
		{}

		FBox Bounds;
		FTransform Transform;
		const UActorDescContainer* Container;
		EContainerClusterMode ClusterMode;
		FActorDescViewMap ActorDescViewMap;
		TSet<FName> RuntimeDataLayers;
		FString OwnerName;
	};

	/** Maps containers IDs to their container descriptor */
	TMap<FActorContainerID, FContainerDescriptor> ContainerDescriptorsMap;

	/** Maps containers IDs to their parent ID */
	TMap<FActorContainerID, FActorContainerID> ContainersHierarchy;
};

bool UWorldPartition::GenerateStreaming(TArray<FString>* OutPackagesToGenerate)
{
	FActorDescList* ModifiedActorsDescList = nullptr;

	FStreamingGenerationLogErrorHandler LogErrorHandler;
	FStreamingGenerationMapCheckErrorHandler MapCheckErrorHandler;	
	IStreamingGenerationErrorHandler* ErrorHandler = &LogErrorHandler;	

	if (bIsPIE)
	{
		ModifiedActorsDescList = &RuntimeHash->ModifiedActorDescListForPIE;
		
		// In PIE, we always want to populate the map check dialog
		ErrorHandler = &MapCheckErrorHandler;
	}

	// Dump state log
	const TCHAR* StateLogSuffix = bIsPIE ? TEXT("PIE") : (IsRunningGame() ? TEXT("Game") : (IsRunningCookCommandlet() ? TEXT("Cook") : TEXT("Manual")));
	TUniquePtr<FArchive> LogFileAr = FWorldPartitionStreamingGenerator::CreateDumpStateLogArchive(StateLogSuffix);
	FHierarchicalLogArchive HierarchicalLogAr(*LogFileAr);

	FActorClusterContext ActorClusterContext;
	FWorldPartitionStreamingGenerator StreamingGenerator(ModifiedActorsDescList, ErrorHandler, IsStreamingEnabled());

	// Preparation Phase
	StreamingGenerator.PreparationPhase(this);

	StreamingGenerator.DumpStateLog(HierarchicalLogAr);

	// Preparation Phase :: Actor Clusters Creation
	ActorClusterContext = StreamingGenerator.CreateActorClusters();

	// Generate streaming
	check(!StreamingPolicy);
	StreamingPolicy = NewObject<UWorldPartitionStreamingPolicy>(const_cast<UWorldPartition*>(this), WorldPartitionStreamingPolicyClass.Get(), NAME_None, bIsPIE ? RF_Transient : RF_NoFlags);

	check(RuntimeHash);
	if (RuntimeHash->GenerateStreaming(StreamingPolicy, ActorClusterContext, OutPackagesToGenerate))
	{
		if (IsRunningCookCommandlet())
		{
			RuntimeHash->DumpStateLog(HierarchicalLogAr);
		}

		StreamingPolicy->PrepareActorToCellRemapping();
		return true;
	}

	return false;
}

void UWorldPartition::FlushStreaming()
{
	RuntimeHash->FlushStreaming();
	StreamingPolicy = nullptr;
}

void UWorldPartition::GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly)
{
	FStreamingGenerationLogErrorHandler LogErrorHandler;
	FWorldPartitionStreamingGenerator StreamingGenerator(nullptr, &LogErrorHandler, IsStreamingEnabled());
	StreamingGenerator.PreparationPhase(this);

	TUniquePtr<FArchive> LogFileAr = FWorldPartitionStreamingGenerator::CreateDumpStateLogArchive(TEXT("HLOD"));
	FHierarchicalLogArchive HierarchicalLogAr(*LogFileAr);
	StreamingGenerator.DumpStateLog(HierarchicalLogAr);

	// Preparation Phase :: Actor Clusters Creation
	FActorClusterContext ActorClusterContext = StreamingGenerator.CreateActorClusters([](const FWorldPartitionActorDescView& ActorDescView)
	{
		return !ActorDescView.GetActorNativeClass()->IsChildOf<AWorldPartitionHLOD>();
	});

	RuntimeHash->GenerateHLOD(SourceControlHelper, ActorClusterContext, bCreateActorsOnly);
}

void UWorldPartition::CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const
{
	CheckForErrors(ErrorHandler, this, IsStreamingEnabled());
}

void UWorldPartition::CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler, const UActorDescContainer* ActorDescContainer, bool bEnableStreaming)
{
	FActorDescList ModifiedActorDescList;
	FWorldPartitionStreamingGenerator StreamingGenerator(ActorDescContainer->GetWorld() ? &ModifiedActorDescList : nullptr, ErrorHandler, bEnableStreaming);
	StreamingGenerator.PreparationPhase(ActorDescContainer);
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE