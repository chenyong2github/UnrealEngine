// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorCluster.h"

#if WITH_EDITOR

#include "Algo/Transform.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

template<class LayerNameContainer>
TSet<const UDataLayer*> GetDataLayers(UWorld* InWorld, const LayerNameContainer& DataLayerNames)
{
	TSet<const UDataLayer*> DataLayers;
	if (const AWorldDataLayers* WorldDataLayers = InWorld->GetWorldDataLayers())
	{
		for (const FName& DataLayerName : DataLayerNames)
		{
			if (const UDataLayer* DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayerName))
			{
				if (DataLayer->IsRuntime())
				{
					DataLayers.Add(DataLayer);
				}
			}
		}
	}
	return DataLayers;
}

FActorCluster::FActorCluster(UWorld* InWorld, const FWorldPartitionActorDescView& InActorDescView)
	: GridPlacement(InActorDescView.GetGridPlacement())
	, RuntimeGrid(InActorDescView.GetRuntimeGrid())
	, Bounds(InActorDescView.GetBounds())
{
	check(GridPlacement != EActorGridPlacement::None);

	Actors.Add(InActorDescView.GetGuid());
	DataLayers = GetDataLayers(InWorld, InActorDescView.GetDataLayers());
	DataLayersID = FDataLayersID(DataLayers.Array());
}

void FActorCluster::Add(const FActorCluster& InActorCluster, const TMap<FGuid, FWorldPartitionActorDescView>& InActorDescViewMap)
{
	// Merge RuntimeGrid
	if (RuntimeGrid != InActorCluster.RuntimeGrid)
	{
		RuntimeGrid = NAME_None;
	}

	// Merge Bounds
	Bounds += InActorCluster.Bounds;

	// Merge GridPlacement
	if (GridPlacement != EActorGridPlacement::AlwaysLoaded)
	{
		GridPlacement = (InActorCluster.GridPlacement == EActorGridPlacement::AlwaysLoaded) ? EActorGridPlacement::AlwaysLoaded : EActorGridPlacement::Bounds;
	}

	if (DataLayersID != InActorCluster.DataLayersID)
	{
		auto LogActorGuid = [&InActorDescViewMap](const FGuid& ActorGuid)
		{
			const FWorldPartitionActorDescView* ActorDescView = InActorDescViewMap.Find(ActorGuid);
			UE_LOG(LogWorldPartition, Verbose, TEXT("   - Actor: %s (%s)"), ActorDescView ? *ActorDescView->GetActorPath().ToString() : TEXT("None"), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid));
		};

		auto LogDataLayers = [](const TSet<const UDataLayer*>& InDataLayers)
		{
			TArray<FString> DataLayerLabels;
			Algo::Transform(InDataLayers, DataLayerLabels, [](const UDataLayer* DataLayer) { return DataLayer->GetDataLayerLabel().ToString(); });
			UE_LOG(LogWorldPartition, Verbose, TEXT("   - DataLayers: %s"), *FString::Join(DataLayerLabels, TEXT(", ")));
		};

		if (DataLayers.Num() && InActorCluster.DataLayers.Num())
		{
			UE_SUPPRESS(LogWorldPartition, Verbose,
			{
				UE_LOG(LogWorldPartition, Verbose, TEXT("Merging Data Layers for clustered actors with different sets of Data Layers."));
				UE_LOG(LogWorldPartition, Verbose, TEXT("1st cluster :"));
				LogDataLayers(DataLayers);
				for (const FGuid& ActorGuid : Actors)
				{
					LogActorGuid(ActorGuid);
				}
				UE_LOG(LogWorldPartition, Verbose, TEXT("2nd cluster :"));
				LogDataLayers(InActorCluster.DataLayers);
				for (const FGuid& ActorGuid : InActorCluster.Actors)
				{
					LogActorGuid(ActorGuid);
				}
			});

			// Merge Data Layers
			for (const UDataLayer* DataLayer : InActorCluster.DataLayers)
			{
				check(DataLayer->IsRuntime());
				DataLayers.Add(DataLayer);
			}
		}
		else
		{
			// In the case where one of the actor cluster has no Data Layer and the other does, 
			// the merged actor cluster removes all Data Layers
			UE_SUPPRESS(LogWorldPartition, Verbose,
			{
				UE_LOG(LogWorldPartition, Verbose, TEXT("Removing Data Layers for clustered actors because they are referenced by or are referencing other actors with no Data Layer."));
				UE_LOG(LogWorldPartition, Verbose, TEXT("Clustered actors with Data Layers :"));
				const FActorCluster * ActorClusterWithDataLayer = DataLayers.Num() ? this : &InActorCluster;
				for (const FGuid& ActorGuid : ActorClusterWithDataLayer->Actors)
				{
					LogActorGuid(ActorGuid);
				}
				UE_LOG(LogWorldPartition, Verbose, TEXT("Clustered actors without Data Layer :"));
				const FActorCluster * ActorClusterWithoutDataLayer = DataLayers.Num() ? &InActorCluster : this;
				for (const FGuid& ActorGuid : ActorClusterWithoutDataLayer->Actors)
				{
					LogActorGuid(ActorGuid);
				}
			});
			DataLayers.Empty();
		}
		DataLayersID = FDataLayersID(DataLayers.Array());
	}

	// Merge Actors
	Actors.Append(InActorCluster.Actors);
}

FActorClusterInstance::FActorClusterInstance(const FActorCluster* InCluster, const FActorContainerInstance* InContainerInstance)
	: Cluster(InCluster)
	, ContainerInstance(InContainerInstance)
{
	Bounds = Cluster->Bounds.TransformBy(ContainerInstance->Transform);
	
	TSet<const UDataLayer*> DataLayerSet;
	DataLayerSet.Reserve(Cluster->DataLayers.Num() + ContainerInstance->DataLayers.Num());
	// If the ContainerInstance is a WorldPartition we want the Cluster DataLayers to be propagated to the ClusterInstance
	if (InContainerInstance->Container->IsA<UWorldPartition>())
	{
		DataLayerSet.Append(Cluster->DataLayers);
	}
	// We also want to propagate the ContainerInstance DataLayers to the ClusterInstance always
	if (ContainerInstance->DataLayers.Num())
	{
		DataLayerSet.Append(ContainerInstance->DataLayers);	
	}
	DataLayers.Reserve(DataLayerSet.Num());
	DataLayers.Append(DataLayerSet.Array());
}

FActorClusterContext::FActorClusterContext(TArray<FActorContainerInstance>&& InContainerInstances, FFilterActorDescViewFunc InFilterActorDescViewFunc)
	: FilterActorDescViewFunc(InFilterActorDescViewFunc)
	, ContainerInstances(MoveTemp(InContainerInstances))
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateActorClusters);

	for (const FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		const TArray<FActorCluster>& NewClusters = CreateActorClusters(ContainerInstance);
		for (const FActorCluster& Cluster : NewClusters)
		{
			ClusterInstances.Emplace(&Cluster, &ContainerInstance);
		}
	}
}

FActorContainerInstance::FActorContainerInstance(const FActorContainerID& InID, const FTransform& InTransform, const FBox& InBounds, const TSet<FName>& InDataLayers, EContainerClusterMode InClusterMode, const UActorDescContainer* InContainer, TMap<FGuid, FWorldPartitionActorDescView> InActorDescViewMap)
	: ID(InID)
	, Transform(InTransform)
	, Bounds(InBounds)
	, ClusterMode(InClusterMode)
	, Container(InContainer)
	, ActorDescViewMap(InActorDescViewMap)
{
	DataLayers = GetDataLayers(InContainer->GetWorld(), InDataLayers);
}

const FWorldPartitionActorDescView& FActorContainerInstance::GetActorDescView(const FGuid& InGuid) const
{
	return ActorDescViewMap.FindChecked(InGuid);
}

FActorInstance::FActorInstance()
	: ContainerInstance(nullptr)
{}

FActorInstance::FActorInstance(const FGuid& InActor, const FActorContainerInstance* InContainerInstance)
	: Actor(InActor)
	, ContainerInstance(InContainerInstance)
{
	check(ContainerInstance);
}

FVector FActorInstance::GetOrigin() const
{
	return ContainerInstance->Transform.TransformPosition(GetActorDescView().GetOrigin());
}

const FWorldPartitionActorDescView& FActorInstance::GetActorDescView() const
{
	return ContainerInstance->GetActorDescView(Actor);
}

void CreateActorCluster(const FWorldPartitionActorDescView& ActorDescView, TMap<FGuid, FActorCluster*>& ActorToActorCluster, TSet<FActorCluster*>& ActorClustersSet, UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView>& ActorDescViewMap)
{
	const FGuid& ActorGuid = ActorDescView.GetGuid();

	FActorCluster* ActorCluster = ActorToActorCluster.FindRef(ActorGuid);
	if (!ActorCluster)
	{
		ActorCluster = new FActorCluster(World, ActorDescView);
		ActorClustersSet.Add(ActorCluster);
		ActorToActorCluster.Add(ActorGuid, ActorCluster);
	}

	for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
	{
		if (const FWorldPartitionActorDescView* ReferenceActorDescView = ActorDescViewMap.Find(ReferenceGuid))
		{
			FActorCluster* ReferenceCluster = ActorToActorCluster.FindRef(ReferenceGuid);
			if (ReferenceCluster)
			{
				if (ReferenceCluster != ActorCluster)
				{
					// Merge reference cluster in Actor's cluster
					ActorCluster->Add(*ReferenceCluster, ActorDescViewMap);
					for (const FGuid& ReferenceClusterActorGuid : ReferenceCluster->Actors)
					{
						ActorToActorCluster[ReferenceClusterActorGuid] = ActorCluster;
					}
					ActorClustersSet.Remove(ReferenceCluster);
					delete ReferenceCluster;
				}
			}
			else
			{
				// Put Reference in Actor's cluster
				ActorCluster->Add(FActorCluster(World, *ReferenceActorDescView), ActorDescViewMap);
			}

			// Map its cluster
			ActorToActorCluster.Add(ReferenceGuid, ActorCluster);
		}
	}
}

FActorContainerInstance* FActorClusterContext::GetClusterInstance(const UActorDescContainer* InContainer)
{
	for (FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		if (ContainerInstance.Container == InContainer)
		{
			return &ContainerInstance;
		}
	}

	return nullptr;
}

const FActorContainerInstance* FActorClusterContext::GetClusterInstance(const UActorDescContainer* InContainer) const
{
	for (const FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		if (ContainerInstance.Container == InContainer)
		{
			return &ContainerInstance;
		}
	}

	return nullptr;
}

void FActorClusterContext::CreateActorClusters(UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView>& ActorDescViewMap, TArray<FActorCluster>& OutActorClusters, FActorClusterContext::FFilterActorDescViewFunc InFilterActorDescViewFunc)
{
	TMap<FGuid, FActorCluster*> ActorToActorCluster;
	TSet<FActorCluster*> ActorClustersSet;

	for (auto& ActorDescViewPair : ActorDescViewMap)
	{
		const FWorldPartitionActorDescView& ActorDescView = ActorDescViewPair.Value;

		if (!InFilterActorDescViewFunc || InFilterActorDescViewFunc(ActorDescView))
		{
			CreateActorCluster(ActorDescView, ActorToActorCluster, ActorClustersSet, World, ActorDescViewMap);
		}
	}

	OutActorClusters.Reserve(ActorClustersSet.Num());
	Algo::Transform(ActorClustersSet, OutActorClusters, [](FActorCluster* ActorCluster) { return MoveTemp(*ActorCluster); });
	for (FActorCluster* ActorCluster : ActorClustersSet) { delete ActorCluster; }
}

void FActorClusterContext::CreateActorClusters(UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView>& ActorDescViewMap, TArray<FActorCluster>& OutActorClusters)
{
	CreateActorClusters(World, ActorDescViewMap, OutActorClusters, nullptr);
}

const TArray<FActorCluster>& FActorClusterContext::CreateActorClusters(const FActorContainerInstance& ContainerInstance)
{
	if (TArray<FActorCluster>* FoundClusters = Clusters.Find(ContainerInstance.Container))
	{
		return *FoundClusters;
	}
		
	TArray<FActorCluster>& ActorClusters = Clusters.Add(ContainerInstance.Container);
	
	CreateActorClusters(ContainerInstance.Container->GetWorld(), ContainerInstance.ActorDescViewMap, ActorClusters, FilterActorDescViewFunc);
		
	return ActorClusters;
}
#endif // #if WITH_EDITOR
