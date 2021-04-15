// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR

#include "WorldPartition/DataLayer/DataLayersID.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "Misc/HashBuilder.h"

class UActorDescContainer;
class UWorldPartition;
class FWorldPartitionActorDesc;
class UWorldPartitionRuntimeHash;
enum class EContainerClusterMode : uint8;

/**
 * List of actors bound together based on clustering rules. (mainly object references)
 */
struct FActorCluster
{
	TSet<FGuid>					Actors;
	EActorGridPlacement			GridPlacement;
	FName						RuntimeGrid;
	FBox						Bounds;
	TSet<const UDataLayer*>		DataLayers;
	FDataLayersID				DataLayersID;

	FActorCluster(UWorld* InWorld, const FWorldPartitionActorDescView& InActorDescView);
	void Add(const FActorCluster& InActorCluster);

};

struct FActorClusterInstance;

/**
 * Instance of a container (level) with specific instance properties (transform, id, datalayers) 
 */
struct FActorContainerInstance
{
	FActorContainerInstance(const UActorDescContainer* InContainer, TMap<FGuid, FWorldPartitionActorDescView> InActorDescViewMap);
	FActorContainerInstance(uint32 InID, const FTransform& InTransform, const FBox& InBounds, const TSet<FName>& InDataLayers, EContainerClusterMode InClusterMode, const UActorDescContainer* InContainer, TSet<FGuid> InChildContainers, TMap<FGuid, FWorldPartitionActorDescView> InActorDescViewMap);
	
	uint32						ID;
	FTransform					Transform;
	FBox						Bounds;
	EContainerClusterMode		ClusterMode;
	const UActorDescContainer*	Container;
	TSet<FGuid>					ChildContainers;
	TMap<FGuid, FWorldPartitionActorDescView> ActorDescViewMap;
	TSet<const UDataLayer*>		DataLayers;

	const FWorldPartitionActorDescView& GetActorDescView(const FGuid& InGuid) const;
};

/**
 *  Represents an instanced actor cluster. It is a combination of the clustering of actors (link between actors agnostic of instance) and a container instance (transform, instance data layers).
 */
struct FActorClusterInstance
{
	FActorClusterInstance(const FActorCluster* InCluster, const FActorContainerInstance* InContainerInstance);

	FBox							Bounds;
	const FActorCluster*			Cluster;
	const FActorContainerInstance*	ContainerInstance;
	TArray<const UDataLayer*>		DataLayers;
};

/**
 * Represents one actor and his owning container instance 
 */
struct FActorInstance
{
	FGuid Actor;
	const FActorContainerInstance* ContainerInstance;

	FActorInstance();
	FActorInstance(const FGuid& InActor, const FActorContainerInstance* InContainerInstance);

	FVector GetOrigin() const;

	friend uint32 GetTypeHash(const FActorInstance& InActorInstance)
	{
		FHashBuilder HashBuilder;
		HashBuilder << InActorInstance.Actor;
		HashBuilder << InActorInstance.ContainerInstance->ID;
		return HashBuilder.GetHash();
	}

	friend bool operator==(const FActorInstance& A, const FActorInstance& B)
	{
		return A.Actor == B.Actor && A.ContainerInstance == B.ContainerInstance;
	}

	bool ShouldStripFromStreaming() const;

	const FWorldPartitionActorDescView& GetActorDescView() const;
};

/**
 * Class used to generate the actor clustering. 
 */
class FActorClusterContext
{
public:
	typedef TFunctionRef<bool(const FWorldPartitionActorDescView&)> FFilterPredicate;

	/**
	 * Create the actor clusters from the root World Partition. Optionally filtering some actors and including child Containers. 
	 */
	FActorClusterContext(const UWorldPartition* InWorldPartition, const UWorldPartitionRuntimeHash* InRuntimeHash, TOptional<FFilterPredicate> InFilterPredicate = TOptional<FFilterPredicate>(), bool bInIncludeChildContainers = true);
				
	/**
	 * Returns the list of cluster instances of this context. 
	 */
	const TArray<FActorClusterInstance>& GetClusterInstances() const { return ClusterInstances; }
	
	FActorContainerInstance* GetClusterInstance(const UActorDescContainer* InContainer);

private:
	void CreateActorClusters();
	void CreateContainerInstanceRecursive(uint32 ID, const FTransform& Transform, EContainerClusterMode ClusterMode, const UActorDescContainer* ActorDescContainer, const TSet<FName>& DataLayers, FBox& ParentBounds);
	const TArray<FActorCluster>& CreateActorClustersImpl(const FActorContainerInstance& ContainerInstance);
	
	// Init data
	const UWorldPartition* WorldPartition;
	const UWorldPartitionRuntimeHash* RuntimeHash;
	TOptional<FFilterPredicate> FilterPredicate;
	bool bIncludeChildContainers;

	// Generated data
	TMap<const UActorDescContainer*, TArray<FActorCluster>> Clusters;
	TArray<FActorContainerInstance> ContainerInstances;
	TArray<FActorClusterInstance> ClusterInstances;
	
	int32 InstanceCountHint;
};

#endif // #if WITH_EDITOR
