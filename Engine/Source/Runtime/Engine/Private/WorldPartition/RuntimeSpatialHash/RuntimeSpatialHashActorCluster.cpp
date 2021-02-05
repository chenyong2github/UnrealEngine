// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/RuntimeSpatialHash/RuntimeSpatialHashActorCluster.h"

#if WITH_EDITOR

#include "Algo/Transform.h"

#include "Engine/World.h"
#include "ActorReferencesUtils.h"
#include "Engine/LevelScriptBlueprint.h"

FActorCluster::FActorCluster(const FWorldPartitionActorDesc* InActorDesc, EActorGridPlacement InGridPlacement, UWorld* InWorld)
	: GridPlacement(InGridPlacement)
	, RuntimeGrid(InActorDesc->GetRuntimeGrid())
	, Bounds(InActorDesc->GetBounds())
{
	check(GridPlacement != EActorGridPlacement::None);

	Actors.Add(InActorDesc->GetGuid());
	if (const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(InWorld))
	{
		for (const FName& DataLayerName : InActorDesc->GetDataLayers())
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

	DataLayersID = FDataLayersID(DataLayers);
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
			DataLayers.AddUnique(DataLayer);
		}
		DataLayersID = FDataLayersID(DataLayers);
	}
}


void CreateActorCluster(const FWorldPartitionActorDesc* ActorDesc, EActorGridPlacement GridPlacement, TMap<FGuid, FActorCluster*>& ActorToActorCluster, TSet<FActorCluster*>& ActorClustersSet, UWorldPartition* WorldPartition)
{
	UWorld* World = WorldPartition->GetWorld();
	const FGuid& ActorGuid = ActorDesc->GetGuid();

	FActorCluster* ActorCluster = ActorToActorCluster.FindRef(ActorGuid);
	if (!ActorCluster)
	{
		ActorCluster = new FActorCluster(ActorDesc, GridPlacement, World);
		ActorClustersSet.Add(ActorCluster);
		ActorToActorCluster.Add(ActorGuid, ActorCluster);
	}

	// Don't include references from editor-only actors
	if (!ActorDesc->GetActorIsEditorOnly())
	{
		for (const FGuid& ReferenceGuid : ActorDesc->GetReferences())
		{
			const FWorldPartitionActorDesc* ReferenceActorDesc = WorldPartition->GetActorDesc(ReferenceGuid);

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
					ActorCluster->Add(FActorCluster(ReferenceActorDesc, GridPlacement, World));
				}

				// Map its cluster
				ActorToActorCluster.Add(ReferenceGuid, ActorCluster);
			}
		}
	}
}

TArray<FActorCluster> CreateActorClustersImpl(UWorldPartition* WorldPartition, TOptional<FFilterPredicate> InFilterPredicate)
{
	TMap<FGuid, FActorCluster*> ActorToActorCluster;
	TSet<FActorCluster*> ActorClustersSet;

	// Gather all references to external actors from the level script
	TSet<AActor*> LevelScriptExternalActorReferences;
	if (ULevelScriptBlueprint* LevelScriptBlueprint = WorldPartition->GetWorld()->PersistentLevel->GetLevelScriptBlueprint(true))
	{
		LevelScriptExternalActorReferences.Append(ActorsReferencesUtils::GetExternalActorReferences(LevelScriptBlueprint));
	}

	for (UActorDescContainer::TIterator<> ActorDescIterator(WorldPartition); ActorDescIterator; ++ActorDescIterator)
	{
		FWorldPartitionActorDesc* ActorDesc = *ActorDescIterator;
		EActorGridPlacement GridPlacement = ActorDesc->GetGridPlacement();

		// Check if the actor is loaded (potentially referenced by the level script)
		if (AActor* Actor = ActorDesc->GetActor())
		{
			if (LevelScriptExternalActorReferences.Contains(Actor))
			{
				GridPlacement = EActorGridPlacement::AlwaysLoaded;
			}
		}

		if (!InFilterPredicate.IsSet() || InFilterPredicate.GetValue()(*ActorDesc))
		{
			CreateActorCluster(ActorDesc, GridPlacement, ActorToActorCluster, ActorClustersSet, WorldPartition);
		}
	}

	TArray<FActorCluster> ActorClusters;
	ActorClusters.Reserve(ActorClustersSet.Num());
	Algo::Transform(ActorClustersSet, ActorClusters, [](FActorCluster* ActorCluster) { return MoveTemp(*ActorCluster); });
	for (FActorCluster* ActorCluster : ActorClustersSet) { delete ActorCluster; }
	return ActorClusters;
}

TArray<FActorCluster> CreateActorClusters(UWorldPartition* WorldPartition, const FFilterPredicate& InFilterPredicate)
{
	return CreateActorClustersImpl(WorldPartition, InFilterPredicate);
}

TArray<FActorCluster> CreateActorClusters(UWorldPartition* WorldPartition)
{
	return CreateActorClustersImpl(WorldPartition, TOptional<FFilterPredicate>());
}

#endif // #if WITH_EDITOR