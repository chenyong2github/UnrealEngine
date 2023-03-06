// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionStreamingGeneration.h"
#include "Misc/HierarchicalLogArchive.h"
#include "WorldPartition/WorldPartitionStreamingGenerationContext.h"

#include "Editor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ActorReferencesUtils.h"
#include "Misc/PackageName.h"
#include "ReferenceCluster.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/DataLayerUtils.h"
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

UWorldPartition::FCheckForErrorsParams::FCheckForErrorsParams()
	: ErrorHandler(nullptr)
	, ActorDescContainer(nullptr)
	, bEnableStreaming(false)
{}

class FWorldPartitionStreamingGenerator
{
	class FStreamingGenerationContext : public IStreamingGenerationContext
	{
	public:
		FStreamingGenerationContext(const FWorldPartitionStreamingGenerator* StreamingGenerator, const UActorDescContainer* MainWorldContainer)
		{
			// Create the dataset required for IStreamingGenerationContext interface
			MainWorldActorSetContainerIndex = INDEX_NONE;
			ActorSetContainers.Empty(StreamingGenerator->ContainerDescriptorsMap.Num());

			TMap<const UActorDescContainer*, int32> ActorSetContainerMap;
			for (const auto& [ActorDescContainer, ContainerDescriptor] : StreamingGenerator->ContainerDescriptorsMap)
			{
				const int32 ContainerIndex = ActorSetContainers.AddDefaulted();
				ActorSetContainerMap.Add(ContainerDescriptor.Container, ContainerIndex);

				FActorSetContainer& ActorSetContainer = ActorSetContainers[ContainerIndex];
				ActorSetContainer.ActorDescViewMap = &ContainerDescriptor.ActorDescViewMap;
				ActorSetContainer.ActorDescContainer = ContainerDescriptor.Container;

				ActorSetContainer.ActorSets.Empty(ContainerDescriptor.Clusters.Num());
				for (const TArray<FGuid>& Cluster : ContainerDescriptor.Clusters)
				{
					FActorSet& ActorSet = *ActorSetContainer.ActorSets.Add_GetRef(MakeUnique<FActorSet>()).Get();
					ActorSet.Actors = Cluster;
				}

				if (ContainerDescriptor.Container == MainWorldContainer)
				{
					check(MainWorldActorSetContainerIndex == INDEX_NONE);
					MainWorldActorSetContainerIndex = ContainerIndex;
				}
			}

			ActorSetInstances.Empty();
			for (const auto& [ContainerID, ContainerInstanceDescriptor] : StreamingGenerator->ContainerInstanceDescriptorsMap)
			{
				const FContainerDescriptor& ContainerDescriptor = StreamingGenerator->ContainerDescriptorsMap.FindChecked(ContainerInstanceDescriptor.Container);
				const FActorSetContainer& ActorSetContainer = ActorSetContainers[ActorSetContainerMap.FindChecked(ContainerInstanceDescriptor.Container)];

				for (const TUniquePtr<FActorSet>& ActorSetPtr : ActorSetContainer.ActorSets)
				{
					const FActorSet& ActorSet = *ActorSetPtr;
					const FWorldPartitionActorDescView& ReferenceActorDescView = ActorSetContainer.ActorDescViewMap->FindByGuidChecked(ActorSet.Actors[0]);

					// Validate assumptions
					for (const FGuid& ActorGuid : ActorSet.Actors)
					{
						const FWorldPartitionActorDescView& ActorDescView = ActorSetContainer.ActorDescViewMap->FindByGuidChecked(ActorGuid);
						check(ActorDescView.GetRuntimeGrid() == ReferenceActorDescView.GetRuntimeGrid());
						check(ActorDescView.GetIsSpatiallyLoaded() == ReferenceActorDescView.GetIsSpatiallyLoaded());
						check(ActorDescView.GetContentBundleGuid() == ReferenceActorDescView.GetContentBundleGuid());
					}

					FActorSetInstance& ActorSetInstance = ActorSetInstances.Emplace_GetRef();
				
					ActorSetInstance.ContainerInstance = &ActorSetContainer;
					ActorSetInstance.ActorSet = &ActorSet;
					ActorSetInstance.ContainerID = ContainerInstanceDescriptor.ID;
					ActorSetInstance.Transform = ContainerInstanceDescriptor.Transform;

					// Apply AND logic on spatially loaded flag
					ActorSetInstance.bIsSpatiallyLoaded = ReferenceActorDescView.GetIsSpatiallyLoaded() && ContainerInstanceDescriptor.bIsSpatiallyLoaded;

					// Since Content Bundles streaming generation happens in its own context, all actor set instances must have the same content bundle GUID for now, so Level Instances
					// placed inside a Content Bundle will propagate their Content Bundle GUID to child instances.
					ActorSetInstance.ContentBundleID = MainWorldContainer->GetContentBundleGuid();

					if (ContainerInstanceDescriptor.ID.IsMainContainer())
					{
						// Main container will get inherited properties from the actor descriptors
						ActorSetInstance.RuntimeGrid = ReferenceActorDescView.GetRuntimeGrid();
						ActorSetInstance.DataLayers = StreamingGenerator->GetRuntimeDataLayerInstances(ReferenceActorDescView.GetRuntimeDataLayerInstanceNames());
					}
					else
					{
						// Sub containers will inherit some properties from the parent actor descriptors
						ActorSetInstance.RuntimeGrid = ContainerInstanceDescriptor.RuntimeGrid;
						TSet<FName> CombinedDataLayers = ContainerInstanceDescriptor.RuntimeDataLayers;
						CombinedDataLayers.Append(ReferenceActorDescView.GetRuntimeDataLayerInstanceNames());
						ActorSetInstance.DataLayers = StreamingGenerator->GetRuntimeDataLayerInstances(CombinedDataLayers.Array());
					}

					ActorSetInstance.Bounds.Init();
					for (const FGuid& ActorGuid : ActorSetInstance.ActorSet->Actors)
					{
						const FWorldPartitionActorDescView& ActorDescView = ActorSetContainer.ActorDescViewMap->FindByGuidChecked(ActorGuid);
						ActorSetInstance.Bounds += ActorDescView.GetRuntimeBounds().TransformBy(ContainerInstanceDescriptor.Transform);
					}
				}
			}

			WorldBounds = StreamingGenerator->ContainerInstanceDescriptorsMap.FindChecked(FActorContainerID::GetMainContainerID()).Bounds;
		}

		virtual ~FStreamingGenerationContext()
		{}

		//~Begin IStreamingGenerationContext interface
		virtual FBox GetWorldBounds() const override
		{
			return WorldBounds;
		}

		virtual const FActorSetContainer* GetMainWorldContainer() const override
		{
			return ActorSetContainers.IsValidIndex(MainWorldActorSetContainerIndex) ? &ActorSetContainers[MainWorldActorSetContainerIndex] : nullptr;
		}

		virtual void ForEachActorSetInstance(TFunctionRef<void(const FActorSetInstance&)> Func) const override
		{
			for (const FActorSetInstance& ActorSetInstance : ActorSetInstances)
			{
				Func(ActorSetInstance);
			}
		}

		virtual void ForEachActorSetContainer(TFunctionRef<void(const FActorSetContainer&)> Func) const override
		{
			for (const FActorSetContainer& ActorSetContainer : ActorSetContainers)
			{
				Func(ActorSetContainer);
			}
		}
		//~End IStreamingGenerationContext interface};

	private:
		FBox WorldBounds;
		int32 MainWorldActorSetContainerIndex;
		TArray<FActorSetContainer> ActorSetContainers;
		TArray<FActorSetInstance> ActorSetInstances;
	};

	struct FContainerDescriptor
	{
		FContainerDescriptor()
			: Container(nullptr)
		{}

		const UActorDescContainer* Container;
		FActorDescViewMap ActorDescViewMap;
		FActorDescViewMap FilteredActorDescViewMap;
		TArray<FWorldPartitionActorDescView> ContainerInstanceViews;
		TArray<TArray<FGuid>> Clusters;
	};

	struct FContainerInstanceDescriptor
	{
		FContainerInstanceDescriptor()
			: Bounds(ForceInit)
			, Container(nullptr)
		{}

		FBox Bounds;
		FTransform Transform;
		const UActorDescContainer* Container;
		EContainerClusterMode ClusterMode;
		TSet<FName> RuntimeDataLayers;
		FName RuntimeGrid;
		bool bIsSpatiallyLoaded;
		FString OwnerName;
		FActorContainerID ID;
		FActorContainerID ParentID;
	};

	void ResolveRuntimeSpatiallyLoaded(FWorldPartitionActorDescView& ActorDescView)
	{
		if (!bEnableStreaming)
		{
			ActorDescView.SetForcedNonSpatiallyLoaded();
		}
	}

	void ResolveRuntimeGrid(FWorldPartitionActorDescView& ActorDescView)
	{
		if (!bEnableStreaming)
		{
			ActorDescView.SetForcedNoRuntimeGrid();
		}
	}

	void ResolveRuntimeDataLayers(FWorldPartitionActorDescView& ActorDescView, const FActorDescViewMap& ActorDescViewMap)
	{
		const UDataLayerManager* DataLayerManager = WorldPartitionContext ? WorldPartitionContext->GetDataLayerManager() : nullptr;

		// Resolve DataLayerInstanceNames of ActorDescView only when necessary (i.e. when container is a template)
		if (!ActorDescView.GetActorDesc()->HasResolvedDataLayerInstanceNames())
		{
			// Build a WorldDataLayerActorDescs if DataLayerManager can't resolve Data Layers (i.e. when validating changelists and World is not loaded)
			const bool bDataLayerManagerCanResolve = DataLayerManager && DataLayerManager->CanResolveDataLayers();
			const TArray<const FWorldDataLayersActorDesc*> WorldDataLayerActorDescs = !bDataLayerManagerCanResolve ? FDataLayerUtils::FindWorldDataLayerActorDescs(ActorDescViewMap) : TArray<const FWorldDataLayersActorDesc*>();
			const TArray<FName> DataLayerInstanceNames = FDataLayerUtils::ResolvedDataLayerInstanceNames(DataLayerManager, ActorDescView.GetActorDesc(), WorldDataLayerActorDescs);
			ActorDescView.SetDataLayerInstanceNames(DataLayerInstanceNames);
		}

		TArray<FName> RuntimeDataLayerInstanceNames;
		if (FDataLayerUtils::ResolveRuntimeDataLayerInstanceNames(DataLayerManager, ActorDescView, ActorDescViewMap, RuntimeDataLayerInstanceNames))
		{
			ActorDescView.SetRuntimeDataLayerInstanceNames(RuntimeDataLayerInstanceNames);
		}
	}

	void CreateActorDescViewMap(const UActorDescContainer* InContainer, FActorDescViewMap& OutActorDescViewMap, FActorDescViewMap& OutFilteredActorDescViewMap, const FActorContainerID& InContainerID, TArray<FWorldPartitionActorDescView>& OutContainerInstances)
	{
		// Should we handle unsaved or newly created actors?
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

		auto IsFilteredActorClass = [this](const FWorldPartitionActorDesc* ActorDesc)
		{
			for (UClass* FilteredClass : FilteredClasses)
			{
				if (ActorDesc->GetActorNativeClass()->IsChildOf(FilteredClass))
				{
					return true;
				}
			}
			return false;
		};

		// Create an actor descriptor view for the specified actor (modified or unsaved actors)
		auto GetModifiedActorDesc = [this](AActor* InActor, const UActorDescContainer* InContainer) -> FWorldPartitionActorDesc*
		{
			FWorldPartitionActorDesc* ModifiedActorDesc = ModifiedActorsDescList->AddActor(InActor);

			// Pretend that this actor descriptor belongs to the original container, even if it's not present. It's essentially a proxy
			// descriptor on top an existing one and at this point no code should require to access the container to resolve it anyways.
			ModifiedActorDesc->SetContainer(const_cast<UActorDescContainer*>(InContainer), InActor->GetWorld());

			return ModifiedActorDesc;
		};

		// Register the actor descriptor view
		auto RegisterActorDescView = [this, InContainer, &OutActorDescViewMap, &OutContainerInstances](const FGuid& ActorGuid, FWorldPartitionActorDescView& InActorDescView)
		{
			if (InActorDescView.IsContainerInstance())
			{
				OutContainerInstances.Add(InActorDescView);
			}
			else
			{
				OutActorDescViewMap.Emplace(ActorGuid, InActorDescView);
			}
		};
		
		TMap<FGuid, FGuid> ContainerGuidsRemap;
		for (FActorDescList::TConstIterator<> ActorDescIt(InContainer); ActorDescIt; ++ActorDescIt)
		{
			if (!IsActorEditorOnly(*ActorDescIt, InContainerID) && !IsFilteredActorClass(*ActorDescIt))
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
			else
			{
				FWorldPartitionActorDescView ActorDescView(*ActorDescIt);
				OutFilteredActorDescViewMap.Emplace(ActorDescIt->GetGuid(), ActorDescView);
			}
		}

		// Append new unsaved actors for the persistent level
		if (bHandleUnsavedActors)
		{
			for (AActor* Actor : InContainer->GetWorldPartition()->GetWorld()->PersistentLevel->Actors)
			{
				if (IsValid(Actor) && Actor->IsPackageExternal() && Actor->IsMainPackageActor() && !Actor->IsEditorOnly() && (Actor->GetContentBundleGuid() == InContainer->GetContentBundleGuid()) && !InContainer->GetActorDesc(Actor->GetActorGuid()))
				{
					FWorldPartitionActorDescView ModifiedActorDescView = GetModifiedActorDesc(Actor, InContainer);
					RegisterActorDescView(Actor->GetActorGuid(), ModifiedActorDescView);
				}
			}
		}
	}

	void CreateActorDescriptorViewsRecursive(const FContainerInstanceDescriptor& InContainerInstanceDescriptor)
	{
		// Create or resolve container descriptor
		FContainerDescriptor& ContainerDescriptor = ContainerDescriptorsMap.FindOrAdd(InContainerInstanceDescriptor.Container);
		
		// Scope ContainerInstanceDescriptor so that it doesn't get used mistakenly after the ContainerInstanceViews loop
		{
			// Create container instance descriptor
			check(!ContainerInstanceDescriptorsMap.Contains(InContainerInstanceDescriptor.ID));

			FContainerInstanceDescriptor& ContainerInstanceDescriptor = ContainerInstanceDescriptorsMap.Add(InContainerInstanceDescriptor.ID, InContainerInstanceDescriptor);

			if (!ContainerDescriptor.Container)
			{
				ContainerDescriptor.Container = ContainerInstanceDescriptor.Container;

				// Gather actor descriptor views for this container
				CreateActorDescViewMap(ContainerInstanceDescriptor.Container, ContainerDescriptor.ActorDescViewMap, ContainerDescriptor.FilteredActorDescViewMap, ContainerInstanceDescriptor.ID, ContainerDescriptor.ContainerInstanceViews);

				// Resolve actor descriptor views before validation
				ResolveContainerDescriptor(ContainerDescriptor);

				// Validate container, fixing anything illegal, etc.
				ValidateContainerDescriptor(ContainerDescriptor, ContainerInstanceDescriptor.ID.IsMainContainer());

				// Update container, computing cluster, bounds, etc.
				UpdateContainerDescriptor(ContainerDescriptor);
			}

			// Calculate Bounds of Non-container ActorDescViews
			check(!ContainerInstanceDescriptor.Bounds.IsValid);
			ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([&ContainerInstanceDescriptor](const FWorldPartitionActorDescView& ActorDescView)
			{
				if (ActorDescView.GetIsSpatiallyLoaded())
				{
					ContainerInstanceDescriptor.Bounds += ActorDescView.GetRuntimeBounds().TransformBy(ContainerInstanceDescriptor.Transform);
				}
			});
		}

		// Inherited parent properties logic
		auto InheritParentContainerProperties = [](const FActorContainerID& InParentContainerID, const FWorldPartitionActorDescView& InParentActorDescView, TSet<FName>& InOutRuntimeDataLayers, FName& InOutRuntimeGrid, bool& bInOutIsSpatiallyLoaded)
		{
			// Runtime grid is only inherited from the main world, since level instance doesn't support setting this value on actors
			if (InParentContainerID.IsMainContainer())
			{
				InOutRuntimeGrid = InParentActorDescView.GetRuntimeGrid();
			}

			// Apply AND logic on spatially loaded flag
			bInOutIsSpatiallyLoaded = bInOutIsSpatiallyLoaded && InParentActorDescView.GetIsSpatiallyLoaded();

			// Data layers are accumulated down the hierarchy chain, since level instances supports data layers assignation on actors
			InOutRuntimeDataLayers.Append(InParentActorDescView.GetRuntimeDataLayerInstanceNames());
		};

		// Parse actor containers
		TArray<FWorldPartitionActorDescView> ContainerInstanceViews = ContainerDescriptor.ContainerInstanceViews;
		for (const FWorldPartitionActorDescView& ContainerInstanceView : ContainerInstanceViews)
		{
			const FGuid ActorGuid = ContainerInstanceView.GetGuid();
			FWorldPartitionActorDesc::FContainerInstance SubContainerInstance(FActorContainerID(InContainerInstanceDescriptor.ID, ActorGuid));
			if (!ContainerInstanceView.GetContainerInstance(SubContainerInstance))
			{
				ErrorHandler->OnLevelInstanceInvalidWorldAsset(ContainerInstanceView, ContainerInstanceView.GetLevelPackage(), IStreamingGenerationErrorHandler::ELevelInstanceInvalidReason::WorldAssetHasInvalidContainer);
				continue;
			}

			if (ContainerInstancesStack.Contains(SubContainerInstance.Container->ContainerPackageName))
			{
				ErrorHandler->OnLevelInstanceInvalidWorldAsset(ContainerInstanceView, ContainerInstanceView.GetLevelPackage(), IStreamingGenerationErrorHandler::ELevelInstanceInvalidReason::CirculalReference);
				continue;
			}

			ContainerInstancesStack.Add(SubContainerInstance.Container->ContainerPackageName);

			check(SubContainerInstance.Container);
			FContainerInstanceDescriptor SubContainerInstanceDescriptor;

			// Inherit fields from FContainerInstance
			SubContainerInstanceDescriptor.ID = SubContainerInstance.GetID();
			SubContainerInstanceDescriptor.Container = SubContainerInstance.Container;
			SubContainerInstanceDescriptor.Transform = SubContainerInstance.Transform * InContainerInstanceDescriptor.Transform;
			SubContainerInstanceDescriptor.ParentID = InContainerInstanceDescriptor.ID;
			SubContainerInstanceDescriptor.OwnerName = *ContainerInstanceView.GetActorLabelOrName().ToString();

			// Inherit parent container properties
			FName InheritedRuntimeGrid = InContainerInstanceDescriptor.RuntimeGrid;
			bool bInheritedbIsSpatiallyLoaded = InContainerInstanceDescriptor.bIsSpatiallyLoaded;
			TSet<FName> InheritedRuntimeDataLayers = InContainerInstanceDescriptor.RuntimeDataLayers;
			InheritParentContainerProperties(InContainerInstanceDescriptor.ID, ContainerInstanceView, InheritedRuntimeDataLayers, InheritedRuntimeGrid, bInheritedbIsSpatiallyLoaded);

			SubContainerInstanceDescriptor.bIsSpatiallyLoaded = bInheritedbIsSpatiallyLoaded;
			SubContainerInstanceDescriptor.RuntimeGrid = InheritedRuntimeGrid;
			SubContainerInstanceDescriptor.RuntimeDataLayers = InheritedRuntimeDataLayers;

			CreateActorDescriptorViewsRecursive(SubContainerInstanceDescriptor);

			verify(ContainerInstancesStack.Remove(SubContainerInstance.Container->ContainerPackageName));
		}
							
		if (!InContainerInstanceDescriptor.ID.IsMainContainer())
		{
			FContainerInstanceDescriptor& ParentContainer = ContainerInstanceDescriptorsMap.FindChecked(InContainerInstanceDescriptor.ParentID);
			
			// Fetch version stored in map as it will be the only one that will have its bounds updated by its Children
			const FContainerInstanceDescriptor& ContainerInstanceWithUpdatedBounds = ContainerInstanceDescriptorsMap.FindChecked(InContainerInstanceDescriptor.ID);
			ParentContainer.Bounds += ContainerInstanceWithUpdatedBounds.Bounds;
		}
	}

	/** 
	 * Creates the actor descriptor views for the specified container.
	 */
	void CreateActorContainers(const UActorDescContainer* InContainer)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionStreamingGenerator::CreateActorContainers);

		// Since we apply AND logic on spatially loaded flag recursively, startup value must be true
		FContainerInstanceDescriptor MainContainerInstance;
		MainContainerInstance.Container = InContainer;
		MainContainerInstance.bIsSpatiallyLoaded = true;
		MainContainerInstance.ClusterMode = EContainerClusterMode::Partitioned;
		MainContainerInstance.OwnerName = TEXT("MainContainer");
		CreateActorDescriptorViewsRecursive(MainContainerInstance);
	}

	/** 
	 * Perform various validations on the container descriptor, and adjust it based on different requirements. This needs to happen before updating
	 * containers bounds because some actor descriptor views might change grid placement, etc.
	 */
	void ResolveContainerDescriptor(FContainerDescriptor& ContainerDescriptor)
	{
		auto ResolveActorDescView = [this, &ContainerDescriptor](FWorldPartitionActorDescView& ActorDescView)
		{
			ResolveRuntimeSpatiallyLoaded(ActorDescView);
			ResolveRuntimeGrid(ActorDescView);
			ResolveRuntimeDataLayers(ActorDescView, ContainerDescriptor.ActorDescViewMap);
		};

		ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([this, &ResolveActorDescView](FWorldPartitionActorDescView& ActorDescView)
		{
			ResolveActorDescView(ActorDescView);
		});

		for (FWorldPartitionActorDescView& ContainerInstanceView : ContainerDescriptor.ContainerInstanceViews)
		{
			ResolveActorDescView(ContainerInstanceView);
		}
	}

	/** 
	 * Perform various validations on the container descriptor, and adjust it based on different requirements. This needs to happen before updating
	 * containers bounds because some actor descriptor views might change grid placement, etc.
	 */
	void ValidateContainerDescriptor(FContainerDescriptor& ContainerDescriptor, bool bIsMainContainer)
	{
		if (bIsMainContainer)
		{
			TArray<FGuid> LevelScriptReferences;
			if (WorldPartitionContext)
			{
				// Gather all references to external actors from the level script and make them always loaded
				if (ULevelScriptBlueprint* LevelScriptBlueprint = WorldPartitionContext->GetTypedOuter<UWorld>()->PersistentLevel->GetLevelScriptBlueprint(true))
				{
					TArray<AActor*> LevelScriptExternalActorReferences = ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint);
					Algo::Transform(LevelScriptExternalActorReferences, LevelScriptReferences, [](const AActor* Actor) { return Actor->GetActorGuid(); });
				}

				// Validate data layers
				if (const UDataLayerManager* DataLayerManager = WorldPartitionContext ? WorldPartitionContext->GetDataLayerManager() : nullptr)
				{
					DataLayerManager->ForEachDataLayerInstance([this](const UDataLayerInstance* DataLayerInstance)
					{
						DataLayerInstance->Validate(ErrorHandler);
						return true;
					});
				}
			}
			else
			{
				ULevel::GetLevelScriptExternalActorsReferencesFromPackage(ContainerDescriptor.Container->GetContainerPackage(), LevelScriptReferences);
			}

			for (const FGuid& LevelScriptReferenceActorGuid : LevelScriptReferences)
			{
				if (FWorldPartitionActorDescView* ActorDescView = ContainerDescriptor.ActorDescViewMap.FindByGuid(LevelScriptReferenceActorGuid))
				{
					if (ActorDescView->GetIsSpatiallyLoaded())
					{
						ErrorHandler->OnInvalidReferenceLevelScriptStreamed(*ActorDescView);
						ActorDescView->SetForcedNonSpatiallyLoaded();
					}

					if (ActorDescView->GetRuntimeDataLayerInstanceNames().Num())
					{
						ErrorHandler->OnInvalidReferenceLevelScriptDataLayers(*ActorDescView);
						ActorDescView->SetInvalidDataLayers();
					}
				}
			}
		}

		// Route standard CheckForErrors calls which should not modify actor descriptors in any ways
		ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([this](FWorldPartitionActorDescView& ActorDescView)
		{
			ActorDescView.CheckForErrors(ErrorHandler);
		});

		// Perform various adjustements based on validations and report errors
		//
		// The first validation pass is used to report errors, subsequent passes are used to make corrections to the actor descriptor views.
		// Since the references can form cycles/long chains in the data fixes might need to be propagated in multiple passes.
		// 
		// This works because fixes are deterministic and always apply the same way to both Actors being modified, so there's no ordering issues possible.
		int32 NbErrorsDetected = INDEX_NONE;
		for(uint32 NbValidationPasses = 0; NbErrorsDetected; NbValidationPasses++)
		{
			// Type of work performed in this pass, for clarity
			enum class EPassType
			{
				ErrorReporting,
				Fixup
			};
			const EPassType PassType = NbValidationPasses == 0 ? EPassType::ErrorReporting : EPassType::Fixup;

			NbErrorsDetected = 0;

			ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([this, &ContainerDescriptor, &NbErrorsDetected, PassType](FWorldPartitionActorDescView& ActorDescView)
			{
				// Validate grid placement
				auto IsReferenceGridPlacementValid = [](const FWorldPartitionActorDescView& RefererActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
				{
					const bool bIsActorDescSpatiallyLoaded = RefererActorDescView.GetIsSpatiallyLoaded();
					const bool bIsActorDescRefSpatiallyLoaded = ReferenceActorDescView.GetIsSpatiallyLoaded();
					return bIsActorDescSpatiallyLoaded == bIsActorDescRefSpatiallyLoaded;
				};

				// Validate data layers
				auto IsReferenceDataLayersValid = [](const FWorldPartitionActorDescView& RefererActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
				{
					if (RefererActorDescView.GetRuntimeDataLayerInstanceNames().Num() == ReferenceActorDescView.GetRuntimeDataLayerInstanceNames().Num())
					{
						const TSet<FName> RefererActorDescDataLayers(RefererActorDescView.GetRuntimeDataLayerInstanceNames());
						const TSet<FName> ReferenceActorDescDataLayers(ReferenceActorDescView.GetRuntimeDataLayerInstanceNames());

						return RefererActorDescDataLayers.Includes(ReferenceActorDescDataLayers);
					}

					return false;
				};

				// Validate runtime grid
				auto IsRuntimeGridValid = [this](const FWorldPartitionActorDescView& ActorDescView)
				{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
					if (!ActorDescView.ShouldValidateRuntimeGrid())
					{
						return true;
					}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

					return IsValidGrid(ActorDescView.GetRuntimeGrid());
				};

				// Validate runtime grid references
				auto IsReferenceRuntimeGridValid = [](const FWorldPartitionActorDescView& RefererActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
				{
					return RefererActorDescView.GetRuntimeGrid() == ReferenceActorDescView.GetRuntimeGrid();
				};

				if (!IsRuntimeGridValid(ActorDescView))
				{
					if (PassType == EPassType::ErrorReporting)
					{
						ErrorHandler->OnInvalidRuntimeGrid(ActorDescView, ActorDescView.GetRuntimeGrid());
					}
					else
					{
						ActorDescView.SetForcedNoRuntimeGrid();
					}
				}

				// Build references List
				struct FActorReferenceInfo
				{
					FGuid ActorGuid;
					FWorldPartitionActorDescView* ActorDesc;
					FGuid ReferenceGuid;
					FWorldPartitionActorDescView* ReferenceActorDesc;
				};

				TArray<FActorReferenceInfo> References;

				// Add normal actor references
				for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
				{
					if (ReferenceGuid != ActorDescView.GetParentActor()) // References to the parent are inversed in their handling 
					{
						// Filter out parent back references
						FWorldPartitionActorDescView* ReferenceActorDesc = ContainerDescriptor.ActorDescViewMap.FindByGuid(ReferenceGuid);
						if (!ReferenceActorDesc || (ReferenceActorDesc->GetParentActor() != ActorDescView.GetGuid()))
						{
							References.Emplace(FActorReferenceInfo { ActorDescView.GetGuid(), &ActorDescView, ReferenceGuid, ReferenceActorDesc });
						}
					}
				}

				// Add attach reference for the topmost parent, this reference is inverted since we consider the top most existing 
				// parent to be refering to us, not the child to be referering the parent.
				{
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
							if (PassType == EPassType::ErrorReporting)
							{
								// We had a guid but parent cannot be found, this will report a missing reference error, but no error in the subsequent passes
								References.Emplace(FActorReferenceInfo{ ActorDescView.GetGuid(), &ActorDescView, ParentGuid, nullptr });
							}

							break; 
						}
					}

					if (TopParentDescView)
					{
						References.Emplace(FActorReferenceInfo { TopParentDescView->GetGuid(), TopParentDescView, ActorDescView.GetGuid(), &ActorDescView });
					}
				}

				TArray<FGuid> RuntimeReferences;
				if (PassType == EPassType::Fixup)
				{
					RuntimeReferences.Reserve(ActorDescView.GetReferences().Num());
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
							if (PassType == EPassType::ErrorReporting)
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
							if (PassType == EPassType::ErrorReporting)
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
							if (PassType == EPassType::ErrorReporting)
							{
								ErrorHandler->OnInvalidReferenceRuntimeGrid(*RefererActorDescView, *ReferenceActorDescView);
							}
							else
							{
								RefererActorDescView->SetForcedNoRuntimeGrid();
								ReferenceActorDescView->SetForcedNoRuntimeGrid();
							}

							NbErrorsDetected++;
						}

						if (PassType == EPassType::Fixup)
						{
							RuntimeReferences.Add(Info.ReferenceGuid);
						}
					}
					else
					{
						if (!ContainerDescriptor.FilteredActorDescViewMap.FindByGuid(Info.ReferenceGuid))
						{
							if (PassType == EPassType::ErrorReporting)
							{
								FWorldPartitionActorDescView ReferendceActorDescView;
								FWorldPartitionActorDescView* ReferendceActorDescViewPtr = nullptr;

								if (const UActorDescContainer** ExistingReferenceContainerPtr = ActorGuidsToContainerMap.Find((Info.ReferenceGuid)))
								{
									if (const FWorldPartitionActorDesc* ReferenceActorDesc = (*ExistingReferenceContainerPtr)->GetActorDesc(Info.ReferenceGuid))
									{
										ReferendceActorDescView = FWorldPartitionActorDescView(ReferenceActorDesc);
										ReferendceActorDescViewPtr = &ReferendceActorDescView;
									}
								}

								ErrorHandler->OnInvalidReference(*RefererActorDescView, Info.ReferenceGuid, ReferendceActorDescViewPtr);
							}
						}

						NbErrorsDetected++;
					}
				}

				if (PassType == EPassType::Fixup)
				{
					if (RuntimeReferences.Num() != ActorDescView.GetReferences().Num())
					{
						ActorDescView.SetRuntimeReferences(RuntimeReferences);
					}
				}
			});		
		}
	}

	/** 
	 * Update the container descriptor containers to adjust their bounds from actor descriptor views.
	 */
	void UpdateContainerDescriptor(FContainerDescriptor& ContainerDescriptor)
	{
		// Build clusters for this container - at this point, all actors references should be in the same data layers, grid, etc because of actor descriptors validation.
		TArray<TPair<FGuid, TArray<FGuid>>> ActorsWithRefs;
		ContainerDescriptor.ActorDescViewMap.ForEachActorDescView([&ActorsWithRefs](const FWorldPartitionActorDescView& ActorDescView) { ActorsWithRefs.Emplace(ActorDescView.GetGuid(), ActorDescView.GetReferences()); });
		ContainerDescriptor.Clusters = GenerateObjectsClusters(ActorsWithRefs);
	}

public:
	struct FWorldPartitionStreamingGeneratorParams
	{
		FWorldPartitionStreamingGeneratorParams()
			: WorldPartitionContext(nullptr)
			, ModifiedActorsDescList(nullptr)
			, ErrorHandler(&NullErrorHandler)
			, bEnableStreaming(false)
		{}

		const UWorldPartition* WorldPartitionContext;
		FActorDescList* ModifiedActorsDescList;
		IStreamingGenerationErrorHandler* ErrorHandler;
		bool bEnableStreaming;
		TMap<FGuid, const UActorDescContainer*> ActorGuidsToContainerMap;
		TArray<TSubclassOf<AActor>> FilteredClasses;
		TFunction<bool(FName)> IsValidGrid;

		inline static FStreamingGenerationNullErrorHandler NullErrorHandler;
	};

	FWorldPartitionStreamingGenerator(const FWorldPartitionStreamingGeneratorParams& Params)
		: WorldPartitionContext(Params.WorldPartitionContext)
		, bEnableStreaming(Params.bEnableStreaming)
		, ModifiedActorsDescList(Params.ModifiedActorsDescList)
		, FilteredClasses(Params.FilteredClasses)
		, IsValidGrid(Params.IsValidGrid)
		, ErrorHandler(Params.ErrorHandler)
		, ActorGuidsToContainerMap(Params.ActorGuidsToContainerMap)
	{}

	void PreparationPhase(const UActorDescContainer* Container)
	{
		CreateActorContainers(Container);

		// Construct the streaming generation context
		StreamingGenerationContext = MakeUnique<FStreamingGenerationContext>(this, Container);
	}

	static TUniquePtr<FArchive> CreateDumpStateLogArchive(const TCHAR* Suffix)
	{
		if (!GIsBuildMachine)
		{
			FString StateLogOutputFilename = FPaths::ProjectSavedDir() / TEXT("WorldPartition") / FString::Printf(TEXT("StreamingGeneration-%s"), Suffix);
			StateLogOutputFilename += FString::Printf(TEXT("-%08x-%s"), FPlatformProcess::GetCurrentProcessId(), *FDateTime::Now().ToIso8601().Replace(TEXT(":"), TEXT(".")));
			StateLogOutputFilename += TEXT(".log");
			return TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*StateLogOutputFilename));
		}

		return MakeUnique<FArchive>();
	}

	void DumpStateLog(FHierarchicalLogArchive& Ar)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionStreamingGenerator::DumpStateLog);

		// Build the containers tree representation
		TMultiMap<FActorContainerID, FActorContainerID> InvertedContainersHierarchy;
		for (auto& [ContainerID, ContainerInstanceDescriptor] : ContainerInstanceDescriptorsMap)
		{
			if (!ContainerID.IsMainContainer())
			{
				InvertedContainersHierarchy.Add(ContainerInstanceDescriptor.ParentID, ContainerID);
			}
		}

		UE_SCOPED_INDENT_LOG_ARCHIVE(Ar.PrintfIndent(TEXT("Containers:")));
		for (auto& [ActorDescContainer, ContainerDescriptor] : ContainerDescriptorsMap)
		{
			UE_SCOPED_INDENT_LOG_ARCHIVE(Ar.PrintfIndent(TEXT("Container: %s"), *ContainerDescriptor.Container->GetContainerPackage().ToString()));

			if (ContainerDescriptor.ActorDescViewMap.Num())
			{
				UE_SCOPED_INDENT_LOG_ARCHIVE(Ar.PrintfIndent(TEXT("ActorDescs:")));

				TMap<FGuid, FWorldPartitionActorDescView*> SortedActorDescViewMap = ContainerDescriptor.ActorDescViewMap.ActorDescViewsByGuid;
				SortedActorDescViewMap.KeySort([](const FGuid& GuidA, const FGuid& GuidB) { return GuidA < GuidB; });

				for (auto& [ActorGuid, ActorDescView] : SortedActorDescViewMap)
				{
					Ar.Print(*ActorDescView->ToString());
				}
			}

			if (ContainerDescriptor.Clusters.Num())
			{
				UE_SCOPED_INDENT_LOG_ARCHIVE(Ar.PrintfIndent(TEXT("Clusters:")));

				int ClusterIndex = 0;
				for (TArray<FGuid>& ActorGuids : ContainerDescriptor.Clusters)
				{
					UE_SCOPED_INDENT_LOG_ARCHIVE(Ar.PrintfIndent(TEXT("[%3d]"), ClusterIndex++));
					for (const FGuid& ActorGuid : ActorGuids)
					{
						const FWorldPartitionActorDescView& ActorDescView = ContainerDescriptor.ActorDescViewMap.FindByGuidChecked(ActorGuid);
						Ar.Print(*ActorDescView.ToString());
					}
				}
			}
		}

		Ar.Printf(TEXT("ContainerInstances:"));
		auto DumpContainerInstances = [this, &InvertedContainersHierarchy, &Ar](const FActorContainerID& ContainerID)
		{
			auto DumpContainerInstancesRecursive = [this, &InvertedContainersHierarchy, &Ar](const FActorContainerID& ContainerID, auto& RecursiveFunc) -> void
			{
				const FContainerInstanceDescriptor& ContainerInstanceDescriptor = ContainerInstanceDescriptorsMap.FindChecked(ContainerID);
				
				{
					UE_SCOPED_INDENT_LOG_ARCHIVE(Ar.PrintfIndent(TEXT("%s:"), *ContainerInstanceDescriptor.OwnerName));

					Ar.Printf(TEXT("       ID: 0x%016llx"), ContainerID.ID);
					Ar.Printf(TEXT("   Bounds: %s"), *ContainerInstanceDescriptor.Bounds.ToString());
					Ar.Printf(TEXT("Transform: %s"), *ContainerInstanceDescriptor.Transform.ToString());
					Ar.Printf(TEXT("Container: %s"), *ContainerInstanceDescriptor.Container->GetContainerPackage().ToString());
				}

				TArray<FActorContainerID> ChildContainersIDs;
				InvertedContainersHierarchy.MultiFind(ContainerID, ChildContainersIDs);
				ChildContainersIDs.Sort([](const FActorContainerID& ActorContainerIDA, const FActorContainerID& ActorContainerIDB) { return ActorContainerIDA.ID < ActorContainerIDB.ID; });

				if (ChildContainersIDs.Num())
				{
					UE_SCOPED_INDENT_LOG_ARCHIVE(Ar.PrintfIndent(TEXT("SubContainers:")));
						
					for (const FActorContainerID& ChildContainerID : ChildContainersIDs)
					{
						RecursiveFunc(ChildContainerID, RecursiveFunc);
					}
				}
			};

			DumpContainerInstancesRecursive(ContainerID, DumpContainerInstancesRecursive);
		};

		DumpContainerInstances(FActorContainerID());
	}

	const FStreamingGenerationContext* GetStreamingGenerationContext()
	{
		return StreamingGenerationContext.Get();
	}

	TArray<const UDataLayerInstance*> GetRuntimeDataLayerInstances(const TArray<FName>& RuntimeDataLayers) const
	{
		static TArray<const UDataLayerInstance*> EmptyArray;
		const UDataLayerManager* DataLayerManager = WorldPartitionContext ? WorldPartitionContext->GetDataLayerManager() : nullptr;
		return DataLayerManager ? DataLayerManager->GetRuntimeDataLayerInstances(RuntimeDataLayers) : EmptyArray;
	}

private:
	const UWorldPartition* WorldPartitionContext;
	bool bEnableStreaming;
	FActorDescList* ModifiedActorsDescList;
	TArray<TSubclassOf<AActor>> FilteredClasses;
	TFunction<bool(FName)> IsValidGrid;
	IStreamingGenerationErrorHandler* ErrorHandler;

	/** Maps containers to their container descriptor */
	TMap<const UActorDescContainer*, FContainerDescriptor> ContainerDescriptorsMap;
	
	/** Maps containers IDs to their container instance descriptor */
	TMap<FActorContainerID, FContainerInstanceDescriptor> ContainerInstanceDescriptorsMap;

	/** Data required for streaming generation interface */
	TUniquePtr<FStreamingGenerationContext> StreamingGenerationContext;

	/** List of containers participating in this streaming generation step */
	TMap<FGuid, const UActorDescContainer*> ActorGuidsToContainerMap;

	/** List of current container instances on the stack to detect circular references */
	TSet<FName> ContainerInstancesStack;
};

bool UWorldPartition::GenerateStreaming(TArray<FString>* OutPackagesToGenerate)
{
	OnPreGenerateStreaming.Broadcast(OutPackagesToGenerate);

	return GenerateContainerStreaming(ActorDescContainer, OutPackagesToGenerate);
}

bool UWorldPartition::GenerateContainerStreaming(const UActorDescContainer* InActorDescContainer, TArray<FString>* OutPackagesToGenerate /* = nullptr */)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartition::GenerateContainerStreaming);

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

	const FString ContainerPackageName = InActorDescContainer->ContainerPackageName.ToString();
	FString ContainerShortName = FPackageName::GetShortName(ContainerPackageName);
	if (!ContainerPackageName.StartsWith(TEXT("/Game/")))
	{
		TArray<FString> SplitContainerPath;
		if (ContainerPackageName.ParseIntoArray(SplitContainerPath, TEXT("/")))
		{
			ContainerShortName += TEXT(".");
			ContainerShortName += SplitContainerPath[0];
		}
	}

	UE_SCOPED_TIMER(*FString::Printf(TEXT("GenerateStreaming for '%s'"), *ContainerShortName), LogWorldPartition, Display);

	// Dump state log
	TStringBuilder<256> StateLogSuffix;
	StateLogSuffix += bIsPIE ? TEXT("PIE") : (IsRunningGame() ? TEXT("Game") : (IsRunningCookCommandlet() ? TEXT("Cook") : TEXT("Manual")));
	StateLogSuffix += TEXT("_");
	StateLogSuffix += ContainerShortName;
	TUniquePtr<FArchive> LogFileAr = FWorldPartitionStreamingGenerator::CreateDumpStateLogArchive(*StateLogSuffix);
	FHierarchicalLogArchive HierarchicalLogAr(*LogFileAr);

	FWorldPartitionStreamingGenerator::FWorldPartitionStreamingGeneratorParams StreamingGeneratorParams;
	StreamingGeneratorParams.WorldPartitionContext = this;
	StreamingGeneratorParams.ModifiedActorsDescList = ModifiedActorsDescList;
	StreamingGeneratorParams.IsValidGrid = [this](FName GridName) { return RuntimeHash->IsValidGrid(GridName); };
	StreamingGeneratorParams.ErrorHandler = StreamingGenerationErrorHandlerOverride ? (*StreamingGenerationErrorHandlerOverride)(ErrorHandler) : ErrorHandler;
	StreamingGeneratorParams.bEnableStreaming = IsStreamingEnabled();

	FWorldPartitionStreamingGenerator StreamingGenerator(StreamingGeneratorParams);

	// Preparation Phase
	StreamingGenerator.PreparationPhase(InActorDescContainer);

	StreamingGenerator.DumpStateLog(HierarchicalLogAr);

	// Generate streaming
	check(!StreamingPolicy);
	StreamingPolicy = NewObject<UWorldPartitionStreamingPolicy>(const_cast<UWorldPartition*>(this), WorldPartitionStreamingPolicyClass.Get(), NAME_None, bIsPIE ? RF_Transient : RF_NoFlags);

	check(RuntimeHash);
	if (RuntimeHash->GenerateStreaming(StreamingPolicy, StreamingGenerator.GetStreamingGenerationContext(), OutPackagesToGenerate))
	{
		//if (IsRunningCookCommandlet())
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
	GeneratedStreamingPackageNames.Empty();
}

void UWorldPartition::GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly)
{
	ForEachActorDescContainer([this, &SourceControlHelper, bCreateActorsOnly](UActorDescContainer* InActorDescContainer)
	{
		FStreamingGenerationLogErrorHandler LogErrorHandler;
		FWorldPartitionStreamingGenerator::FWorldPartitionStreamingGeneratorParams StreamingGeneratorParams;
		StreamingGeneratorParams.WorldPartitionContext = this;
		StreamingGeneratorParams.ErrorHandler = StreamingGenerationErrorHandlerOverride ? (*StreamingGenerationErrorHandlerOverride)(&LogErrorHandler) : &LogErrorHandler;
		StreamingGeneratorParams.bEnableStreaming = IsStreamingEnabled();
		StreamingGeneratorParams.FilteredClasses.Add(AWorldPartitionHLOD::StaticClass());
		StreamingGeneratorParams.IsValidGrid = [this](FName GridName) { return RuntimeHash->IsValidGrid(GridName); };

		FWorldPartitionStreamingGenerator StreamingGenerator(StreamingGeneratorParams);
		StreamingGenerator.PreparationPhase(InActorDescContainer);

		TUniquePtr<FArchive> LogFileAr = FWorldPartitionStreamingGenerator::CreateDumpStateLogArchive(TEXT("HLOD"));
		FHierarchicalLogArchive HierarchicalLogAr(*LogFileAr);
		StreamingGenerator.DumpStateLog(HierarchicalLogAr);

		RuntimeHash->GenerateHLOD(SourceControlHelper, StreamingGenerator.GetStreamingGenerationContext(), bCreateActorsOnly);
	});	
}

void UWorldPartition::CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const
{
	FActorDescList ModifiedActorDescList;

	FWorldPartitionStreamingGenerator::FWorldPartitionStreamingGeneratorParams StreamingGeneratorParams;
	StreamingGeneratorParams.WorldPartitionContext = this;
	StreamingGeneratorParams.ModifiedActorsDescList = &ModifiedActorDescList;
	StreamingGeneratorParams.ErrorHandler = StreamingGenerationErrorHandlerOverride ? (*StreamingGenerationErrorHandlerOverride)(ErrorHandler) : ErrorHandler;
	StreamingGeneratorParams.IsValidGrid = [this](FName GridName) { return RuntimeHash->IsValidGrid(GridName); };
	StreamingGeneratorParams.bEnableStreaming = IsStreamingEnabled();

	ForEachActorDescContainer([&StreamingGeneratorParams](const UActorDescContainer* InActorDescContainer)
	{
		for (FActorDescList::TConstIterator<> ActorDescIt(InActorDescContainer); ActorDescIt; ++ActorDescIt)
		{
			check(!StreamingGeneratorParams.ActorGuidsToContainerMap.Contains(ActorDescIt->GetGuid()));
			StreamingGeneratorParams.ActorGuidsToContainerMap.Add(ActorDescIt->GetGuid(), InActorDescContainer);
		}
	});

	ForEachActorDescContainer([this, &StreamingGeneratorParams](const UActorDescContainer* InActorDescContainer)
	{
		check(StreamingGeneratorParams.WorldPartitionContext == InActorDescContainer->GetWorldPartition());
		FWorldPartitionStreamingGenerator StreamingGenerator(StreamingGeneratorParams);
		StreamingGenerator.PreparationPhase(InActorDescContainer);
	});
}

/* Deprecated */
void UWorldPartition::CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler, const UActorDescContainer* ActorDescContainer, bool bEnableStreaming, bool)
{
	FCheckForErrorsParams Params;
	Params.ErrorHandler = ErrorHandler;
	Params.ActorDescContainer = ActorDescContainer;
	Params.bEnableStreaming = bEnableStreaming;

	CheckForErrors(Params);
}

void UWorldPartition::CheckForErrors(const FCheckForErrorsParams& Params)
{
	FActorDescList ModifiedActorDescList;

	FWorldPartitionStreamingGenerator::FWorldPartitionStreamingGeneratorParams StreamingGeneratorParams;
	StreamingGeneratorParams.WorldPartitionContext = Params.ActorDescContainer->GetWorldPartition();
	StreamingGeneratorParams.ModifiedActorsDescList = !Params.ActorDescContainer->IsTemplateContainer() ? &ModifiedActorDescList : nullptr;
	StreamingGeneratorParams.ErrorHandler = StreamingGenerationErrorHandlerOverride ? (*StreamingGenerationErrorHandlerOverride)(Params.ErrorHandler) : Params.ErrorHandler;
	StreamingGeneratorParams.IsValidGrid = [](FName GridName) { return true; };
	StreamingGeneratorParams.bEnableStreaming = Params.bEnableStreaming;
	StreamingGeneratorParams.ActorGuidsToContainerMap = Params.ActorGuidsToContainerMap;

	FWorldPartitionStreamingGenerator StreamingGenerator(StreamingGeneratorParams);

	StreamingGenerator.PreparationPhase(Params.ActorDescContainer);
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
