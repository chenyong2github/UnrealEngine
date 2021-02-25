// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorCluster.h"

#if WITH_EDITOR

#include "Algo/Transform.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "Misc/HashBuilder.h"

DEFINE_LOG_CATEGORY(LogWorldPartitionActorCluster);

template<class LayerNameContainer>
TSet<const UDataLayer*> GetDataLayers(UWorld* InWorld, const LayerNameContainer& DataLayerNames)
{
	TSet<const UDataLayer*> DataLayers;
	if (const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(InWorld))
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

FActorCluster::FActorCluster(UWorld* InWorld, const FWorldPartitionActorDescView& InActorDescView, EActorGridPlacement InGridPlacement)
	: GridPlacement(InGridPlacement)
	, RuntimeGrid(InActorDescView.GetRuntimeGrid())
	, Bounds(InActorDescView.GetBounds())
{
	check(GridPlacement != EActorGridPlacement::None);

	Actors.Add(InActorDescView.GetGuid());
	DataLayers = GetDataLayers(InWorld, InActorDescView.GetDataLayers());
	DataLayersID = FDataLayersID(DataLayers.Array());
}

void FActorCluster::Add(const FActorCluster& InActorCluster)
{
	// Merge Actors
	Actors.Append(InActorCluster.Actors);

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

	// Merge DataLayers
	if (DataLayersID != InActorCluster.DataLayersID)
	{
		for (const UDataLayer* DataLayer : InActorCluster.DataLayers)
		{
			check(DataLayer->IsDynamicallyLoaded());
			DataLayers.Add(DataLayer);
		}
		DataLayersID = FDataLayersID(DataLayers.Array());
	}
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

FActorContainerInstance::FActorContainerInstance(uint32 InID, const FTransform& InTransform, const FBox& InBounds, const TSet<FName>& InDataLayers, EContainerClusterMode InClusterMode, const UActorDescContainer* InContainer, TSet<FGuid> InChildContainers, TMap<FGuid, FWorldPartitionActorDescView> InActorDescViewMap)
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

	const FWorldPartitionActorDescView ActorDescView = GetActorDescView();
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

void CreateActorCluster(const FWorldPartitionActorDescView& ActorDescView, EActorGridPlacement GridPlacement, TMap<FGuid, FActorCluster*>& ActorToActorCluster, TSet<FActorCluster*>& ActorClustersSet, const FActorContainerInstance& ContainerInstance)
{
	const UActorDescContainer* ActorDescContainer = ContainerInstance.Container;
	UWorld* World = ActorDescContainer->GetWorld();
	const FGuid& ActorGuid = ActorDescView.GetGuid();

	FActorCluster* ActorCluster = ActorToActorCluster.FindRef(ActorGuid);
	if (!ActorCluster)
	{
		ActorCluster = new FActorCluster(World, ActorDescView, GridPlacement);
		ActorClustersSet.Add(ActorCluster);
		ActorToActorCluster.Add(ActorGuid, ActorCluster);
	}

	// Don't include references from editor-only actors
	if (!ActorDescView.GetActorIsEditorOnly())
	{
		for (const FGuid& ReferenceGuid : ActorDescView.GetReferences())
		{
			const FWorldPartitionActorDescView& ReferenceActorDescView = ContainerInstance.ActorDescViewMap.FindChecked(ReferenceGuid);

			// Don't include references to editor-only actors
			if (!ReferenceActorDescView.GetActorIsEditorOnly())
			{
				FActorCluster* ReferenceCluster = ActorToActorCluster.FindRef(ReferenceGuid);
				if (ReferenceCluster)
				{
					if (ReferenceCluster != ActorCluster)
					{
						// Merge reference cluster in Actor's cluster
						ActorCluster->Add(*ReferenceCluster);
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
					ActorCluster->Add(FActorCluster(World, ReferenceActorDescView, GridPlacement));
				}

				// Map its cluster
				ActorToActorCluster.Add(ReferenceGuid, ActorCluster);
			}
		}
	}
}

void FActorClusterContext::CreateContainerInstanceRecursive(uint32 ID, const FTransform& Transform, EContainerClusterMode ClusterMode, const UActorDescContainer* Container, const TSet<FName>& DataLayers, FBox* ParentBounds)
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

			FHashBuilder ChildContainerHashBuilder(ID);
			ChildContainerHashBuilder << ActorDescView.GetGuid();
			
			TSet<FName> ChildDataLayers;
			ChildDataLayers.Reserve(DataLayers.Num() + ActorDescView.GetDataLayers().Num());
			// Only propagate ActorDesc DataLayers if we are a Root Container (WorldPartition)
			if (Container->IsA<UWorldPartition>())
			{
				ChildDataLayers.Append(ActorDescView.GetDataLayers());
			}
			// Always inherite parent container DataLayers
			ChildDataLayers.Append(DataLayers);
			CreateContainerInstanceRecursive(ChildContainerHashBuilder.GetHash(), OutTransform * Transform, OutClusterMode, OutContainer, ChildDataLayers, &Bounds);
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
	
	if (ParentBounds)
	{
		*ParentBounds += Bounds;
	}

	UE_LOG(LogWorldPartitionActorCluster, Verbose, TEXT("ContainerInstance (%08x) Bounds (%s) Package (%s)"), ID, *Bounds.TransformBy(Transform).ToString(), *Container->GetContainerPackage().ToString());
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
		EActorGridPlacement GridPlacement = ActorDescView.GetGridPlacement();

		if (!FilterPredicate.IsSet() || FilterPredicate.GetValue()(ActorDescView))
		{
			CreateActorCluster(ActorDescView, GridPlacement, ActorToActorCluster, ActorClustersSet, ContainerInstance);
		}
	}
				
	ActorClusters.Reserve(ActorClustersSet.Num());
	Algo::Transform(ActorClustersSet, ActorClusters, [](FActorCluster* ActorCluster) { return MoveTemp(*ActorCluster); });
	for (FActorCluster* ActorCluster : ActorClustersSet) { delete ActorCluster; }

	return ActorClusters;
}

void FActorClusterContext::CreateActorClusters()
{
	// First Instance is the main WorldPartition
	CreateContainerInstanceRecursive(0, FTransform::Identity, EContainerClusterMode::Partitioned, WorldPartition, TSet<FName>());
			
	ClusterInstances.Reserve(InstanceCountHint);
	for (const FActorContainerInstance& ContainerInstance : ContainerInstances)
	{
		const TArray<FActorCluster>& NewClusters = CreateActorClustersImpl(ContainerInstance);
		for (const FActorCluster& Cluster : NewClusters)
		{
			ClusterInstances.Emplace(&Cluster, &ContainerInstance);
		}
	}
}

#endif // #if WITH_EDITOR