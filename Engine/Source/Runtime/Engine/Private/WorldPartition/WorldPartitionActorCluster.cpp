// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorCluster.h"

#if WITH_EDITOR

#include "Algo/Transform.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/ActorDescContainer.h"
#include "ActorReferencesUtils.h"
#include "Engine/LevelScriptBlueprint.h"
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

FActorCluster::FActorCluster(const FWorldPartitionActorDesc* InActorDesc, EActorGridPlacement InGridPlacement, const UActorDescContainer* InContainer)
	: GridPlacement(InGridPlacement)
	, RuntimeGrid(InActorDesc->GetRuntimeGrid())
	, Bounds(InActorDesc->GetBounds())
{
	check(GridPlacement != EActorGridPlacement::None);

	Actors.Add(InActorDesc->GetGuid());
	DataLayers = GetDataLayers(InContainer->GetWorld(), InActorDesc->GetDataLayers());
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

FActorClusterContext::FActorClusterContext(const UWorldPartition* InWorldPartition, TOptional<FFilterPredicate> InFilterPredicate, bool bInIncludeChildContainers)
	: WorldPartition(InWorldPartition)
	, FilterPredicate(InFilterPredicate)
	, bIncludeChildContainers(bInIncludeChildContainers)
	, InstanceCountHint(0)
{
	CreateActorClusters();
}

FActorContainerInstance::FActorContainerInstance(const UActorDescContainer* InContainer)
	: FActorContainerInstance(0, FTransform::Identity, FBox(ForceInit), TSet<FName>(), EContainerClusterMode::Partitioned, InContainer, TSet<FGuid>())
{

}

FActorContainerInstance::FActorContainerInstance(uint32 InID, const FTransform& InTransform, const FBox& InBounds, const TSet<FName>& InDataLayers, EContainerClusterMode InClusterMode, const UActorDescContainer* InContainer, TSet<FGuid> InChildContainers)
	: ID(InID)
	, Transform(InTransform)
	, Bounds(InBounds)
	, ClusterMode(InClusterMode)
	, Container(InContainer)
	, ChildContainers(InChildContainers)
{
	DataLayers = GetDataLayers(InContainer->GetWorld(), InDataLayers);
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

	const FWorldPartitionActorDesc* ActorDesc = GetActorDesc();
	return ActorDesc->GetActorIsEditorOnly();
}

FVector FActorInstance::GetOrigin() const
{
	return ContainerInstance->Transform.TransformPosition(GetActorDesc()->GetOrigin());
}

const FWorldPartitionActorDesc* FActorInstance::GetActorDesc() const
{
	return ContainerInstance->Container->GetActorDesc(Actor);
}

void CreateActorCluster(const FWorldPartitionActorDesc* ActorDesc, EActorGridPlacement GridPlacement, TMap<FGuid, FActorCluster*>& ActorToActorCluster, TSet<FActorCluster*>& ActorClustersSet, const UActorDescContainer* ActorDescContainer)
{
	UWorld* World = ActorDescContainer->GetWorld();
	const FGuid& ActorGuid = ActorDesc->GetGuid();

	FActorCluster* ActorCluster = ActorToActorCluster.FindRef(ActorGuid);
	if (!ActorCluster)
	{
		ActorCluster = new FActorCluster(ActorDesc, GridPlacement, ActorDescContainer);
		ActorClustersSet.Add(ActorCluster);
		ActorToActorCluster.Add(ActorGuid, ActorCluster);
	}

	// Don't include references from editor-only actors
	if (!ActorDesc->GetActorIsEditorOnly())
	{
		for (const FGuid& ReferenceGuid : ActorDesc->GetReferences())
		{
			if (const FWorldPartitionActorDesc* ReferenceActorDesc = ActorDescContainer->GetActorDesc(ReferenceGuid))
			{
				// Don't include references to editor-only actors
				if (!ReferenceActorDesc->GetActorIsEditorOnly())
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
						ActorCluster->Add(FActorCluster(ReferenceActorDesc, GridPlacement, ActorDescContainer));
					}

					// Map its cluster
					ActorToActorCluster.Add(ReferenceGuid, ActorCluster);
				}
			}
		}
	}
}

void FActorClusterContext::CreateContainerInstanceRecursive(uint32 ID, const FTransform& Transform, EContainerClusterMode ClusterMode , const UActorDescContainer* Container, const TSet<FName>& DataLayers, FBox* ParentBounds)
{
	InstanceCountHint += Container->GetActorDescCount();
		
	TSet<FGuid> ChildContainers;
	FBox Bounds(ForceInit);

	for (UActorDescContainer::TConstIterator<> ActorDescIterator(Container); ActorDescIterator; ++ActorDescIterator)
	{
		const FWorldPartitionActorDesc* ActorDesc = *ActorDescIterator;
		const UActorDescContainer* OutContainer = nullptr;
		FTransform OutTransform;
		EContainerClusterMode OutClusterMode;
		if (bIncludeChildContainers && ActorDesc->GetContainerInstance(OutContainer, OutTransform, OutClusterMode))
		{
			// Add Child Container Guid so we can discard the actor later
			ChildContainers.Add(ActorDesc->GetGuid());

			FHashBuilder ChildContainerHashBuilder(ID);
			ChildContainerHashBuilder << ActorDesc->GetGuid();
			
			TSet<FName> ChildDataLayers;
			ChildDataLayers.Reserve(DataLayers.Num() + ActorDesc->GetDataLayers().Num());
			// Only propagate ActorDesc DataLayers if we are a Root Container (WorldPartition)
			if (Container->IsA<UWorldPartition>())
			{
				ChildDataLayers.Append(ActorDesc->GetDataLayers());
			}
			// Always inherite parent container DataLayers
			ChildDataLayers.Append(DataLayers);
			CreateContainerInstanceRecursive(ChildContainerHashBuilder.GetHash(), OutTransform * Transform, OutClusterMode, OutContainer, ChildDataLayers, &Bounds);
		}
		else
		{
			switch (ActorDesc->GetGridPlacement())
			{
				case EActorGridPlacement::Location:
				{
					FVector Location = ActorDescIterator->GetOrigin();
					Bounds += FBox(Location, Location);
				}
				break;
				case EActorGridPlacement::Bounds:
				{
					Bounds += ActorDescIterator->GetBounds();
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
	ContainerInstances.Add(FActorContainerInstance(ID, Transform, Bounds, DataLayers, ClusterMode, Container, MoveTemp(ChildContainers)));
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
	
	// Gather all references to external actors from the level script
	TSet<AActor*> LevelScriptExternalActorReferences;
	// Only applies to root container
	if (ContainerInstance.Container->IsA<UWorldPartition>())
	{
		if (ULevelScriptBlueprint* LevelScriptBlueprint = ContainerInstance.Container->GetWorld()->PersistentLevel->GetLevelScriptBlueprint(true))
		{
			LevelScriptExternalActorReferences.Append(ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint));
		}
	}
		
	for (UActorDescContainer::TConstIterator<> ActorDescIterator(ContainerInstance.Container); ActorDescIterator; ++ActorDescIterator)
	{
		const FWorldPartitionActorDesc* ActorDesc = *ActorDescIterator;
		EActorGridPlacement GridPlacement = ActorDesc->GetGridPlacement();

		// Check if the actor is loaded (potentially referenced by the level script)
		if (AActor* Actor = ActorDesc->GetActor())
		{
			if (LevelScriptExternalActorReferences.Contains(Actor))
			{
				GridPlacement = EActorGridPlacement::AlwaysLoaded;
			}
		}

		if (!FilterPredicate.IsSet() || FilterPredicate.GetValue()(*ActorDesc))
		{
			CreateActorCluster(ActorDesc, GridPlacement, ActorToActorCluster, ActorClustersSet, ContainerInstance.Container);
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