// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartition.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ActorReferencesUtils.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionActorCluster.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationLogErrorHandler.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationMapCheckErrorHandler.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "WorldPartition"

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
	void CreateActorDescViewMap(const UActorDescContainer* InContainer, TMap<FGuid, FWorldPartitionActorDescView>& OutActorDescViewMap, const FActorContainerID& InContainerID)
	{
		const bool bHandleUnsavedActors = ModifiedActorsDescList && InContainerID.IsMainContainer();

		// Consider all actors of a /Temp/ container package as Unsaved because loading them from disk will fail (Outer world name mismatch)
		const bool bIsTempContainerPackage = FPackageName::IsTempPackage(InContainer->GetPackage()->GetName());
		
		TMap<FGuid, FGuid> ContainerGuidsRemap;
		for (FActorDescList::TConstIterator<> ActorDescIt(InContainer); ActorDescIt; ++ActorDescIt)
		{
			if (!ActorDescIt->GetActorIsEditorOnly())
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
						FWorldPartitionActorDesc* ActorDesc = ModifiedActorsDescList->AddActor(Actor);
						ActorDesc->OnRegister(Actor->GetWorld());
						OutActorDescViewMap.Emplace(ActorDescIt->GetGuid(), ActorDesc);
						continue;
					}
				}

				// Non-dirty actor
				OutActorDescViewMap.Emplace(ActorDescIt->GetGuid(), *ActorDescIt);
			}
		}

		// Append new unsaved actors for the persistent level
		if (bHandleUnsavedActors)
		{
			for (AActor* Actor : InContainer->GetWorld()->PersistentLevel->Actors)
			{
				if (IsValid(Actor) && Actor->IsPackageExternal() && Actor->IsMainPackageActor() && !Actor->IsEditorOnly() && !InContainer->GetActorDesc(Actor->GetActorGuid()))
				{
					FWorldPartitionActorDesc* ActorDesc = ModifiedActorsDescList->AddActor(Actor);
					ActorDesc->OnRegister(Actor->GetWorld());
					OutActorDescViewMap.Emplace(ActorDesc->GetGuid(), ActorDesc);
				}
			}
		}
	}

	void CreateActorDescriptorViewsRecursive(const UActorDescContainer* InContainer, const FTransform& InTransform, const TSet<FName>& InDataLayers, const FActorContainerID& InContainerID, const FActorContainerID& InParentContainerID, EContainerClusterMode InClusterMode, const TCHAR* OwnerName)
	{
		TMap<FGuid, FWorldPartitionActorDescView> ActorDescViewMap;
		
		// Gather actor descriptor views for this container
		CreateActorDescViewMap(InContainer, ActorDescViewMap, InContainerID);

		// Parse actor descriptors
		for (auto It = ActorDescViewMap.CreateIterator(); It; ++It)
		{
			FWorldPartitionActorDescView& ActorDescView = It.Value();

			const UActorDescContainer* SubContainer;
			EContainerClusterMode SubClusterMode;
			FTransform SubTransform;
			if (ActorDescView.GetContainerInstance(SubContainer, SubTransform, SubClusterMode))
			{
				check(SubContainer);

				const FGuid ActorGuid = ActorDescView.GetGuid();
				const FActorContainerID SubContainerID(InContainerID, ActorGuid);

				// Only propagate data layers from the root container as we don't support sub containers to set their own
				const TSet<FName>* SubDataLayers = &InDataLayers;
				TSet<FName> ParentDataLayers;

				if (InContainerID.IsMainContainer())
				{
					ParentDataLayers.Append(ActorDescView.GetDataLayers());
					SubDataLayers = &ParentDataLayers;
				}

				CreateActorDescriptorViewsRecursive(SubContainer, SubTransform * InTransform, *SubDataLayers, SubContainerID, InContainerID, SubClusterMode, *ActorDescView.GetActorLabelOrName().ToString());

				// The container actor can be removed now that its contained actors has been registered
				It.RemoveCurrent();
			}

			if (!InContainerID.IsMainContainer())
			{
				ActorDescViewsContainersMap.Add(ActorDescView.GetGuid(), InContainerID);
			}
		}

		// Create container descriptor
		check(!ContainerDescriptorsMap.Contains(InContainerID));

		FContainerDescriptor& ContainerDescriptor = ContainerDescriptorsMap.Add(InContainerID);
		ContainerDescriptor.Container = InContainer;
		ContainerDescriptor.Transform = InTransform;
		ContainerDescriptor.ClusterMode = InClusterMode;
		ContainerDescriptor.ActorDescViewMap = MoveTemp(ActorDescViewMap);
		ContainerDescriptor.DataLayers = InDataLayers;
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

			if (ContainerID.IsMainContainer())
			{
				// Gather all references to external actors from the level script and make them always loaded
				if (ULevelScriptBlueprint* LevelScriptBlueprint = ContainerDescriptor.Container->GetWorld()->PersistentLevel->GetLevelScriptBlueprint(true))
				{
					TArray<AActor*> LevelScriptExternalActorReferences = ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint);

					for (AActor* Actor : LevelScriptExternalActorReferences)
					{
						FWorldPartitionActorDescView& ActorDescView = ContainerDescriptor.ActorDescViewMap.FindChecked(Actor->GetActorGuid());

						if (ActorDescView.GetGridPlacement() != EActorGridPlacement::AlwaysLoaded)
						{						
							ActorDescView.SetGridPlacement(EActorGridPlacement::AlwaysLoaded);

							if (ErrorHandler)
							{
								ErrorHandler->OnInvalidReferenceLevelScriptStreamed(ActorDescView);
							}
						}

						if (ActorDescView.GetDataLayers().Num())
						{
							ActorDescView.SetInvalidDataLayers();

							if (ErrorHandler)
							{
								ErrorHandler->OnInvalidReferenceLevelScriptDataLayers(ActorDescView);
							}
						}
					}
				}
			}

			// Give the associated runtime hash the possibility to adjust actor descriptor views based on its internal settings, etc.
			RuntimeHash->UpdateActorDescViewMap(ContainerDescriptor.ActorDescViewMap);

			// Perform various adjustements based on validations and errors
			for (auto ActorDescIt = ContainerDescriptor.ActorDescViewMap.CreateIterator(); ActorDescIt; ++ActorDescIt)
			{
				FWorldPartitionActorDescView& ActorDescView = ActorDescIt.Value();

				// Validate references
				for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
				{
					if (FWorldPartitionActorDescView* ReferenceActorDescView = ContainerDescriptor.ActorDescViewMap.Find(ReferenceGuid))
					{
						// Validate grid placement
						const bool bIsActorDescAlwaysLoaded = ActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;
						const bool bIsActorDescRefAlwaysLoaded = ReferenceActorDescView->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;

						if (bIsActorDescAlwaysLoaded != bIsActorDescRefAlwaysLoaded)
						{
							ActorDescView.SetGridPlacement(EActorGridPlacement::AlwaysLoaded);
							ReferenceActorDescView->SetGridPlacement(EActorGridPlacement::AlwaysLoaded);

							if (ErrorHandler)
							{
								ErrorHandler->OnInvalidReferenceGridPlacement(ActorDescView, *ReferenceActorDescView);
							}
						}

						// Validate data layers
						auto IsReferenceDataLayersValid = [](const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
						{
							if (ActorDescView.GetDataLayers().Num() == ReferenceActorDescView.GetDataLayers().Num())
							{
								const TSet<FName> ActorDescDataLayers(ActorDescView.GetDataLayers());
								const TSet<FName> ReferenceActorDescDataLayers(ReferenceActorDescView.GetDataLayers());

								return ActorDescDataLayers.Includes(ReferenceActorDescDataLayers);
							}

							return false;
						};

						if (!IsReferenceDataLayersValid(ActorDescView, *ReferenceActorDescView))
						{
							ActorDescView.SetInvalidDataLayers();
							ReferenceActorDescView->SetInvalidDataLayers();

							if (ErrorHandler)
							{
								ErrorHandler->OnInvalidReferenceDataLayers(ActorDescView, *ReferenceActorDescView);
							}
						}
					}
					else if (ErrorHandler)
					{
						ErrorHandler->OnInvalidReference(ActorDescView, ReferenceGuid);
					}
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

			for (auto ActorDescIt = ContainerDescriptor.ActorDescViewMap.CreateConstIterator(); ActorDescIt; ++ActorDescIt)
			{
				const FWorldPartitionActorDescView& ActorDescView = ActorDescIt.Value();

				switch (ActorDescView.GetGridPlacement())
				{
					case EActorGridPlacement::Location: ContainerDescriptor.Bounds += ContainerDescriptor.Transform.TransformPosition(ActorDescView.GetOrigin()); break;
					case EActorGridPlacement::Bounds: ContainerDescriptor.Bounds += ActorDescView.GetBounds().TransformBy(ContainerDescriptor.Transform); break;
				}
			}
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
	FWorldPartitionStreamingGenerator(UWorldPartitionRuntimeHash* InRuntimeHash, FActorDescList* InModifiedActorsDescList, IStreamingGenerationErrorHandler* InErrorHandler = nullptr)
	: RuntimeHash(InRuntimeHash)
	, ModifiedActorsDescList(InModifiedActorsDescList)
	, ErrorHandler(InErrorHandler)
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

			ContainerInstances.Emplace(ContainerID, ContainerDescriptor.Transform, ContainerDescriptor.Bounds, ContainerDescriptor.DataLayers, ContainerDescriptor.ClusterMode, ContainerDescriptor.Container, ContainerDescriptor.ActorDescViewMap);
		}

		return FActorClusterContext(MoveTemp(ContainerInstances), InFilterActorDescViewFunc);
	}

	void DumpStateLog()
	{
		FString StateLogOutputPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("WorldPartition"));
		FString StateLogOutputFilename = FPaths::Combine(StateLogOutputPath, *FString::Printf(TEXT("StreamingGeneration-%s.log"), *FDateTime::Now().ToString()));
		
		if (FArchive* LogFile = IFileManager::Get().CreateFileWriter(*StateLogOutputFilename))
		{
			auto SerializeLine = [LogFile](const FString& Line)
			{
				LogFile->Serialize(TCHAR_TO_ANSI(*Line), Line.Len());
				LogFile->Serialize(LINE_TERMINATOR_ANSI, sizeof(LINE_TERMINATOR_ANSI));
			};

			// Build the containers tree representation
			TMultiMap<FActorContainerID, FActorContainerID> InvertedContainersHierarchy;
			for (auto ContainerPairIt = ContainersHierarchy.CreateIterator(); ContainerPairIt; ++ContainerPairIt)
			{
				const FActorContainerID& ChildContainerID = ContainerPairIt.Key();
				const FActorContainerID& ParentContainerID = ContainerPairIt.Value();				
				InvertedContainersHierarchy.Add(ParentContainerID, ChildContainerID);
			}

			SerializeLine(TEXT("Containers:"));

			auto DumpContainers = [this, &InvertedContainersHierarchy, &SerializeLine](const FActorContainerID& ContainerID)
			{
				auto DumpContainersRecursive = [this, &InvertedContainersHierarchy, &SerializeLine](const FActorContainerID& ContainerID, FString Prefix, auto& RecursiveFunc) -> void
				{
					const FContainerDescriptor& ContainerDescriptor = ContainerDescriptorsMap.FindChecked(ContainerID);
					SerializeLine(FString::Printf(TEXT("%s[+] %s:"), *Prefix, *ContainerDescriptor.OwnerName));

					Prefix += TEXT(" | ");

					SerializeLine(FString::Printf(TEXT("%s           ID: 0x%016llx"), *Prefix, ContainerID.ID));
					SerializeLine(FString::Printf(TEXT("%s       Bounds: %s"), *Prefix, *ContainerDescriptor.Bounds.ToString()));
					SerializeLine(FString::Printf(TEXT("%s    Transform: %s"), *Prefix, *ContainerDescriptor.Transform.ToString()));
					SerializeLine(FString::Printf(TEXT("%s    Container: %s"), *Prefix, *ContainerDescriptor.Container->GetContainerPackage().ToString()));
					SerializeLine(FString::Printf(TEXT("%s  ClusterMode: %s"), *Prefix, *StaticEnum<EContainerClusterMode>()->GetNameStringByValue((uint64)ContainerDescriptor.ClusterMode)));

					if (ContainerDescriptor.ActorDescViewMap.Num())
					{
						SerializeLine(FString::Printf(TEXT("%s ActorDescs:"), *Prefix));

						TMap<FGuid, FWorldPartitionActorDescView> SortedActorDescViewMap = ContainerDescriptor.ActorDescViewMap;
						SortedActorDescViewMap.KeySort([](const FGuid& GuidA, const FGuid& GuidB) { return GuidA < GuidB; });

						for (auto ActorDescIt = SortedActorDescViewMap.CreateConstIterator(); ActorDescIt; ++ActorDescIt)
						{
							const FWorldPartitionActorDescView& ActorDescView = ActorDescIt.Value();
							SerializeLine(FString::Printf(TEXT("%s     - %s"), *Prefix, *ActorDescView.ToString()));
						}
					}

					TArray<FActorContainerID> ChildContainersIDs;
					InvertedContainersHierarchy.MultiFind(ContainerID, ChildContainersIDs);
					ChildContainersIDs.Sort([](const FActorContainerID& ActorContainerIDA, const FActorContainerID& ActorContainerIDB) { return ActorContainerIDA.ID < ActorContainerIDB.ID; });

					if (ChildContainersIDs.Num())
					{
						SerializeLine(FString::Printf(TEXT("%sSubContainers:"), *Prefix));
						
						Prefix += TEXT("  ");
						for (const FActorContainerID& ChildContainerID : ChildContainersIDs)
						{
							RecursiveFunc(ChildContainerID, Prefix, RecursiveFunc);
						}
					}
				};

				DumpContainersRecursive(ContainerID, TEXT("  "), DumpContainersRecursive);
			};

			DumpContainers(FActorContainerID());

			LogFile->Close();
			delete LogFile;
		}
	}

private:
	UWorldPartitionRuntimeHash* RuntimeHash;
	FActorDescList* ModifiedActorsDescList;
	IStreamingGenerationErrorHandler* ErrorHandler;

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
		TMap<FGuid, FWorldPartitionActorDescView> ActorDescViewMap;
		TSet<FName> DataLayers;
		FString OwnerName;
	};

	/** Maps containers IDs to their container descriptor */
	TMap<FActorContainerID, FContainerDescriptor> ContainerDescriptorsMap;

	/** Maps containers IDs to their parent ID */
	TMap<FActorContainerID, FActorContainerID> ContainersHierarchy;

	/** Maps actor descriptor views to containers IDs */
	TMap<FGuid, FActorContainerID> ActorDescViewsContainersMap;
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

	FActorClusterContext ActorClusterContext;
	{
		FWorldPartitionStreamingGenerator StreamingGenerator(RuntimeHash, ModifiedActorsDescList, ErrorHandler);

		// Preparation Phase
		StreamingGenerator.PreparationPhase(this);
		StreamingGenerator.DumpStateLog();

		// Preparation Phase :: Actor Clusters Creation
		ActorClusterContext = StreamingGenerator.CreateActorClusters();
	}

	// Generate streaming
	check(!StreamingPolicy);
	StreamingPolicy = NewObject<UWorldPartitionStreamingPolicy>(const_cast<UWorldPartition*>(this), WorldPartitionStreamingPolicyClass.Get());

	check(RuntimeHash);
	if (RuntimeHash->GenerateStreaming(StreamingPolicy, ActorClusterContext, OutPackagesToGenerate))
	{
		StreamingPolicy->PrepareActorToCellRemapping();
		return true;
	}

	return false;
}

void UWorldPartition::GenerateHLOD(ISourceControlHelper* SourceControlHelper, bool bCreateActorsOnly)
{
	FStreamingGenerationLogErrorHandler LogErrorHandler;
	FWorldPartitionStreamingGenerator StreamingGenerator(RuntimeHash, nullptr, &LogErrorHandler);
	StreamingGenerator.PreparationPhase(this);

	// Preparation Phase :: Actor Clusters Creation
	FActorClusterContext ActorClusterContext = StreamingGenerator.CreateActorClusters([](const FWorldPartitionActorDescView& ActorDescView)
	{
		return !ActorDescView.GetActorClass()->IsChildOf<AWorldPartitionHLOD>();
	});

	RuntimeHash->GenerateHLOD(SourceControlHelper, ActorClusterContext, bCreateActorsOnly);
}

void UWorldPartition::CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const
{
	FActorClusterContext ActorClusterContext;
	{		
		FActorDescList ModifiedActorDescList;
		FWorldPartitionStreamingGenerator StreamingGenerator(RuntimeHash, &ModifiedActorDescList, ErrorHandler);
		StreamingGenerator.PreparationPhase(this);
	}
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
