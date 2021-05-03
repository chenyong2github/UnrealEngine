// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorCluster.h"

#if WITH_EDITOR

#include "Algo/Transform.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "Hash/CityHash.h"

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
				if (DataLayer->IsDynamicallyLoaded())
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

void FActorCluster::Add(const FActorCluster& InActorCluster, const FActorContainerInstance& ContainerInstance)
{
	// Merge RuntimeGrid
	RuntimeGrid = RuntimeGrid == InActorCluster.RuntimeGrid ? RuntimeGrid : NAME_None;

	// Merge Bounds
	Bounds += InActorCluster.Bounds;

	// Merge GridPlacement
	// If currently None, will always stay None
	if (GridPlacement != EActorGridPlacement::None)
	{
		// If grid placement differs between the two clusters
		if (GridPlacement != InActorCluster.GridPlacement)
		{
			// If one of the two cluster was always loaded, set to None
			if (InActorCluster.GridPlacement == EActorGridPlacement::AlwaysLoaded || GridPlacement == EActorGridPlacement::AlwaysLoaded)
			{
				GridPlacement = EActorGridPlacement::None;
			}
			else
			{
				GridPlacement = InActorCluster.GridPlacement;
			}
		}

		// If current placement is set to Location, that won't make sense when merging two clusters. Set to Bounds
		if (GridPlacement == EActorGridPlacement::Location)
		{
			GridPlacement = EActorGridPlacement::Bounds;
		}
	}

	if (DataLayersID != InActorCluster.DataLayersID)
	{
		auto LogActorGuid = [&ContainerInstance](const FGuid& ActorGuid)
		{
			const FWorldPartitionActorDescView* ActorDescView = ContainerInstance.ActorDescViewMap.Find(ActorGuid);
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
				check(DataLayer->IsDynamicallyLoaded());
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
	// Bounds based on cluster mode
	Bounds = Cluster->Bounds;
	if (ContainerInstance->ClusterMode == EContainerClusterMode::Embedded)
	{
		Bounds = ContainerInstance->Bounds;
	}
	Bounds = Bounds.TransformBy(ContainerInstance->Transform);
	
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

FActorClusterContext::FActorClusterContext(const UWorldPartition* InWorldPartition, const UWorldPartitionRuntimeHash* InRuntimeHash, TOptional<FFilterPredicate> InFilterPredicate, bool bInIncludeChildContainers)
	: WorldPartition(InWorldPartition)
	, RuntimeHash(InRuntimeHash)
	, FilterPredicate(InFilterPredicate)
	, bIncludeChildContainers(bInIncludeChildContainers)
	, InstanceCountHint(0)
{
	CreateActorClusters();
}

FActorContainerInstance::FActorContainerInstance(const UActorDescContainer* InContainer, TMap<FGuid, FWorldPartitionActorDescView> InActorDescViewMap)
	: FActorContainerInstance(0, FTransform::Identity, FBox(ForceInit), TSet<FName>(), EContainerClusterMode::Partitioned, InContainer, TSet<FGuid>(), InActorDescViewMap)
{}

FActorContainerInstance::FActorContainerInstance(uint64 InID, const FTransform& InTransform, const FBox& InBounds, const TSet<FName>& InDataLayers, EContainerClusterMode InClusterMode, const UActorDescContainer* InContainer, TSet<FGuid> InChildContainers, TMap<FGuid, FWorldPartitionActorDescView> InActorDescViewMap)
	: ID(InID)
	, Transform(InTransform)
	, Bounds(InBounds)
	, ClusterMode(InClusterMode)
	, Container(InContainer)
	, ChildContainers(InChildContainers)
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
{

}

FActorInstance::FActorInstance(const FGuid& InActor, const FActorContainerInstance* InContainerInstance)
	: Actor(InActor)
	, ContainerInstance(InContainerInstance)
{
	check(ContainerInstance);
}

bool FActorInstance::ShouldStripFromStreaming() const
{
	// If this Actor instance is a Container itself we strip it
	if (ContainerInstance->ChildContainers.Contains(Actor))
	{
		return true;
	}

	const FWorldPartitionActorDescView& ActorDescView = GetActorDescView();
	return ActorDescView.GetActorIsEditorOnly();
}

FVector FActorInstance::GetOrigin() const
{
	return ContainerInstance->Transform.TransformPosition(GetActorDescView().GetOrigin());
}

const FWorldPartitionActorDescView& FActorInstance::GetActorDescView() const
{
	return ContainerInstance->GetActorDescView(Actor);
}

void CreateActorCluster(const FWorldPartitionActorDescView& ActorDescView, TMap<FGuid, FActorCluster*>& ActorToActorCluster, TSet<FActorCluster*>& ActorClustersSet, const FActorContainerInstance& ContainerInstance)
{
	// Don't include references from editor-only actors
	if (!ActorDescView.GetActorIsEditorOnly())
	{
		const UActorDescContainer* ActorDescContainer = ContainerInstance.Container;
		UWorld* World = ActorDescContainer->GetWorld();
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
			if (const FWorldPartitionActorDescView* ReferenceActorDescView = ContainerInstance.ActorDescViewMap.Find(ReferenceGuid))
			{
				// Don't include references to editor-only actors
				if (!ReferenceActorDescView->GetActorIsEditorOnly())
				{
					FActorCluster* ReferenceCluster = ActorToActorCluster.FindRef(ReferenceGuid);
					if (ReferenceCluster)
					{
						if (ReferenceCluster != ActorCluster)
						{
							// Merge reference cluster in Actor's cluster
							ActorCluster->Add(*ReferenceCluster, ContainerInstance);
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
						ActorCluster->Add(FActorCluster(World, *ReferenceActorDescView), ContainerInstance);
					}

					// Map its cluster
					ActorToActorCluster.Add(ReferenceGuid, ActorCluster);
				}
			}
		}
	}
}

void FActorClusterContext::CreateContainerInstanceRecursive(uint64 ID, const FTransform& Transform, EContainerClusterMode ClusterMode, const UActorDescContainer* Container, const TSet<FName>& DataLayers, FBox& ParentBounds)
{
	InstanceCountHint += Container->GetActorDescCount();
		
	TSet<FGuid> ChildContainers;
	FBox Bounds(ForceInit);

	TMap<FGuid, FWorldPartitionActorDescView> ActorDescViewMap;
	RuntimeHash->CreateActorDescViewMap(Container, ActorDescViewMap);

	for (auto& ActorDesViewPair : ActorDescViewMap)
	{
		FWorldPartitionActorDescView& ActorDescView = ActorDesViewPair.Value;
		const UActorDescContainer* OutContainer = nullptr;
		FTransform OutTransform;
		EContainerClusterMode OutClusterMode;
		if (bIncludeChildContainers && ActorDescView.GetContainerInstance(OutContainer, OutTransform, OutClusterMode))
		{
			// Add Child Container Guid so we can discard the actor later
			ChildContainers.Add(ActorDescView.GetGuid());

			FGuid ActorGuid = ActorDescView.GetGuid();
			uint64 Hash = CityHash64WithSeed((const char*)&ActorGuid, sizeof(ActorGuid), ID);
			
			TSet<FName> ChildDataLayers;
			ChildDataLayers.Reserve(DataLayers.Num() + ActorDescView.GetDataLayers().Num());
			// Only propagate ActorDesc DataLayers if we are a Root Container (WorldPartition)
			if (Container->IsA<UWorldPartition>())
			{
				ChildDataLayers.Append(ActorDescView.GetDataLayers());
			}
			// Always inherite parent container DataLayers
			ChildDataLayers.Append(DataLayers);
			CreateContainerInstanceRecursive(Hash, OutTransform * Transform, OutClusterMode, OutContainer, ChildDataLayers, Bounds);
		}
		else
		{
			switch (ActorDescView.GetGridPlacement())
			{
				case EActorGridPlacement::Location:
				{
					FVector Location = ActorDescView.GetOrigin();
					Bounds += FBox(Location, Location);
				}
				break;
				case EActorGridPlacement::Bounds:
				{
					Bounds += ActorDescView.GetBounds();
				}
				break;
			}
		}
	}
	
	ParentBounds += Bounds;
	
	UE_LOG(LogWorldPartition, Verbose, TEXT("ContainerInstance (%08x) Bounds (%s) Package (%s)"), ID, *Bounds.TransformBy(Transform).ToString(), *Container->GetContainerPackage().ToString());
	ContainerInstances.Add(FActorContainerInstance(ID, Transform, Bounds, DataLayers, ClusterMode, Container, MoveTemp(ChildContainers), MoveTemp(ActorDescViewMap)));
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

const TArray<FActorCluster>& FActorClusterContext::CreateActorClustersImpl(const FActorContainerInstance& ContainerInstance)
{
	if (TArray<FActorCluster>* FoundClusters = Clusters.Find(ContainerInstance.Container))
	{
		return *FoundClusters;
	}

	TMap<FGuid, FActorCluster*> ActorToActorCluster;
	TSet<FActorCluster*> ActorClustersSet;
	TArray<FActorCluster>& ActorClusters = Clusters.Add(ContainerInstance.Container);
	
	for (auto& ActorDescViewPair : ContainerInstance.ActorDescViewMap)
	{
		const FWorldPartitionActorDescView& ActorDescView = ActorDescViewPair.Value;

		if (!FilterPredicate.IsSet() || FilterPredicate.GetValue()(ActorDescView))
		{
			CreateActorCluster(ActorDescView, ActorToActorCluster, ActorClustersSet, ContainerInstance);
		}
	}
				
	ActorClusters.Reserve(ActorClustersSet.Num());
	Algo::Transform(ActorClustersSet, ActorClusters, [](FActorCluster* ActorCluster) { return MoveTemp(*ActorCluster); });
	for (FActorCluster* ActorCluster : ActorClustersSet) { delete ActorCluster; }

	return ActorClusters;
}

void FActorClusterContext::CreateActorClusters()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateActorClusters);

	// First Instance is the main WorldPartition
	FBox WorldBounds(ForceInit);
	CreateContainerInstanceRecursive(0, FTransform::Identity, EContainerClusterMode::Partitioned, WorldPartition, TSet<FName>(), WorldBounds);
		
	ClusterInstances.Reserve(InstanceCountHint);
	for (FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		RuntimeHash->UpdateActorDescViewMap(WorldBounds, ContainerInstance.ActorDescViewMap);

		const TArray<FActorCluster>& NewClusters = CreateActorClustersImpl(ContainerInstance);
		for (const FActorCluster& Cluster : NewClusters)
		{
			ClusterInstances.Emplace(&Cluster, &ContainerInstance);
		}
	}
}

#endif // #if WITH_EDITOR