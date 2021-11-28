// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartition.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "ActorReferencesUtils.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "WorldPartition/WorldPartitionActorCluster.h"
#include "WorldPartition/HLOD/HLODActor.h"
#endif

#define LOCTEXT_NAMESPACE "WorldPartition"

#if WITH_EDITOR

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
		const bool bIncludeUnsavedActors = (bIsPIE && InContainerID.IsMainContainer());

		// Consider all actors of a /Temp/ container package as Unsaved because loading them from disk will fail (Outer world name mismatch)
		const bool bIsTempContainerPackage = FPackageName::IsTempPackage(InContainer->GetPackage()->GetName());
		
		TMap<FGuid, FGuid> ContainerGuidsRemap;
		for (FActorDescList::TConstIterator<> ActorDescIt(InContainer); ActorDescIt; ++ActorDescIt)
		{
			if (!ActorDescIt->GetActorIsEditorOnly())
			{
				AActor* Actor = ActorDescIt->GetActor();

				if (bIncludeUnsavedActors && IsValid(Actor) && (bIsTempContainerPackage || Actor->GetPackage()->IsDirty()))
				{
					// Dirty, unsaved actor for PIE
					FWorldPartitionActorDesc* ActorDesc = RuntimeHash->ModifiedActorDescListForPIE.AddActor(Actor);
					ActorDesc->OnRegister(Actor->GetWorld());
					OutActorDescViewMap.Emplace(ActorDescIt->GetGuid(), ActorDesc);
				}
				else
				{
					// Non-dirty actor
					OutActorDescViewMap.Emplace(ActorDescIt->GetGuid(), *ActorDescIt);
				}
			}
		}

		// Append new unsaved actors for the persistent level
		if (bIncludeUnsavedActors)
		{
			for (AActor* Actor : InContainer->GetWorld()->PersistentLevel->Actors)
			{
				if (IsValid(Actor) && Actor->IsPackageExternal() && Actor->IsMainPackageActor() && !Actor->IsEditorOnly() && !InContainer->GetActorDesc(Actor->GetActorGuid()))
				{
					FWorldPartitionActorDesc* ActorDesc = RuntimeHash->ModifiedActorDescListForPIE.AddActor(Actor);
					ActorDesc->OnRegister(Actor->GetWorld());
					OutActorDescViewMap.Emplace(ActorDesc->GetGuid(), ActorDesc);
				}
			}
		}
	}

	void CreateActorDescriptorViewsRecursive(const UActorDescContainer* InContainer, const FTransform& InTransform, const TSet<FName>& InDataLayers, const FActorContainerID& InContainerID, const FActorContainerID& InParentContainerID, EContainerClusterMode InClusterMode)
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

				CreateActorDescriptorViewsRecursive(SubContainer, SubTransform * InTransform, *SubDataLayers, SubContainerID, InContainerID, SubClusterMode);

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
		CreateActorDescriptorViewsRecursive(InContainer, FTransform::Identity, TSet<FName>(), FActorContainerID(), FActorContainerID(), EContainerClusterMode::Partitioned);
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
						if (FWorldPartitionActorDescView* ActorDescView = ContainerDescriptor.ActorDescViewMap.Find(Actor->GetActorGuid()))
						{
							ActorDescView->SetGridPlacement(EActorGridPlacement::AlwaysLoaded);
						}
					}
				}
			}

			// Give the associated runtime hash to adjust the grid placement based on its internal settings, etc.
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
						if ((ActorDescView.GetGridPlacement() != ReferenceActorDescView->GetGridPlacement()) &&
							((ActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded) || (ReferenceActorDescView->GetGridPlacement() == EActorGridPlacement::AlwaysLoaded)))
						{
							ActorDescView.SetGridPlacement(EActorGridPlacement::AlwaysLoaded);
							ReferenceActorDescView->SetGridPlacement(EActorGridPlacement::AlwaysLoaded);
						}

						// Validate data layers
						if (ActorDescView.GetDataLayers().Num() != ReferenceActorDescView->GetDataLayers().Num())
						{
							ActorDescView.SetInvalidDataLayers();
							ReferenceActorDescView->SetInvalidDataLayers();
						}
						else
						{
							const TSet<FName> ActorDescDataLayers(ActorDescView.GetDataLayers());
							const TSet<FName> ReferenceActorDescDataLayers(ReferenceActorDescView->GetDataLayers());

							if (!ActorDescDataLayers.Includes(ReferenceActorDescDataLayers))
							{
								ActorDescView.SetInvalidDataLayers();
								ReferenceActorDescView->SetInvalidDataLayers();
							}
						}
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
			const FContainerDescriptor& CurrentContainerID = ContainerDescriptorsMap.FindChecked(ContainerPairIt.Key());
			FContainerDescriptor& ParentContainerID = ContainerDescriptorsMap.FindChecked(ContainerPairIt.Value());
			ParentContainerID.Bounds += CurrentContainerID.Bounds;
		}
	}

public:
	FWorldPartitionStreamingGenerator(bool bInIsPIE, UWorldPartitionRuntimeHash* InRuntimeHash)
	: bIsPIE(bInIsPIE)
	, RuntimeHash(InRuntimeHash)
	{}

	void PreparationPhase(UWorldPartition* WorldPartition)
	{
		// Preparation Phase :: Actor Descriptor Views Creation
		CreateActorDescriptorViews(WorldPartition);

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

private:
	bool bIsPIE;
	UWorldPartitionRuntimeHash* RuntimeHash;

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
	FActorClusterContext ActorClusterContext;
	{
		FWorldPartitionStreamingGenerator StreamingGenerator(bIsPIE, RuntimeHash);

		// Preparation Phase
		StreamingGenerator.PreparationPhase(this);

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
	FWorldPartitionStreamingGenerator StreamingGenerator(bIsPIE, RuntimeHash);
	StreamingGenerator.PreparationPhase(this);

	// Preparation Phase :: Actor Clusters Creation
	FActorClusterContext ActorClusterContext = StreamingGenerator.CreateActorClusters([](const FWorldPartitionActorDescView& ActorDescView)
	{
		return !ActorDescView.GetActorClass()->IsChildOf<AWorldPartitionHLOD>();
	});

	RuntimeHash->GenerateHLOD(SourceControlHelper, ActorClusterContext, bCreateActorsOnly);
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE