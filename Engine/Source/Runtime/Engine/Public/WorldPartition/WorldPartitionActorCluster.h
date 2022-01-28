// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "Misc/HashBuilder.h"
#include "Hash/CityHash.h"
#endif

#include "WorldPartitionActorCluster.generated.h"

class UActorDescContainer;
enum class EContainerClusterMode : uint8;

USTRUCT()
struct FActorContainerID
{
	GENERATED_USTRUCT_BODY()

	FActorContainerID()
	: ID(0)
	{}

	FActorContainerID(const FActorContainerID& InOther)
	: ID(InOther.ID)
	{}

	FActorContainerID(const FActorContainerID& InParent, FGuid InActorGuid)
	: ID(CityHash64WithSeed((const char*)&InActorGuid, sizeof(InActorGuid), InParent.ID))
	{}

	void operator=(const FActorContainerID& InOther)
	{
		ID = InOther.ID;
	}

	bool operator==(const FActorContainerID& InOther) const
	{
		return ID == InOther.ID;
	}

	bool operator!=(const FActorContainerID& InOther) const
	{
		return ID != InOther.ID;
	}

	bool IsMainContainer() const
	{
		return !ID;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("%016llx"), ID);
	}

	friend FORCEINLINE uint32 GetTypeHash(const FActorContainerID& InContainerID)
	{
		return GetTypeHash(InContainerID.ID);
	}

	UPROPERTY()
	uint64 ID;
};

#if WITH_EDITOR
/**
 * List of actors bound together based on clustering rules (mainly object references)
 */
struct FActorCluster
{
	TSet<FGuid>					Actors;
	bool						bIsSpatiallyLoaded;
	FName						RuntimeGrid;
	FBox						Bounds;
	TSet<const UDataLayer*>		DataLayers;
	FDataLayersID				DataLayersID;

	FActorCluster(UWorld* InWorld, const FWorldPartitionActorDescView& InActorDescView);
	void Add(const FActorCluster& InActorCluster, const TMap<FGuid, FWorldPartitionActorDescView>& InActorDescViewMap);
};

/**
 * Instance of a container (level) with specific instance properties (transform, id, datalayers) 
 */
struct FActorContainerInstance
{
	FActorContainerInstance(const FActorContainerID& InID, const FTransform& InTransform, const FBox& InBounds, const TSet<FName>& InDataLayers, EContainerClusterMode InClusterMode, const UActorDescContainer* InContainer, TMap<FGuid, FWorldPartitionActorDescView> InActorDescViewMap);
	
	FActorContainerID			ID;
	FTransform					Transform;
	FBox						Bounds;
	EContainerClusterMode		ClusterMode;
	const UActorDescContainer*	Container;
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
struct ENGINE_API FActorInstance
{
	FGuid Actor;
	const FActorContainerInstance* ContainerInstance;

	FActorInstance();
	FActorInstance(const FGuid& InActor, const FActorContainerInstance* InContainerInstance);

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

	const FWorldPartitionActorDescView& GetActorDescView() const;
};

/**
 * Class used to generate the actor clustering. 
 */
class ENGINE_API FActorClusterContext
{
public:
	typedef TFunction<bool(const FWorldPartitionActorDescView&)> FFilterActorDescViewFunc;

	FActorClusterContext() {}

	/**
	 * Create the actor clusters from the root World Partition. Optionally filtering some actors and including child Containers. 
	 */
	FActorClusterContext(TArray<FActorContainerInstance>&& InContainerInstances, FFilterActorDescViewFunc InFilterActorDescViewFunc = nullptr);
				
	/**
	 * Returns the list of cluster instances of this context. 
	 */
	const TArray<FActorClusterInstance>& GetClusterInstances() const { return ClusterInstances; }
	
	const FActorContainerInstance* GetClusterInstance(const FActorContainerID& InContainerID) const;
	FActorContainerInstance* GetClusterInstance(const UActorDescContainer* InContainer);
	const FActorContainerInstance* GetClusterInstance(const UActorDescContainer* InContainer) const;

	static void CreateActorClusters(UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView>& ActorDescViewMap, TArray<FActorCluster>& OutActorClusters);

private:
	const TArray<FActorCluster>& CreateActorClusters(const FActorContainerInstance& ContainerInstance);
	static void CreateActorClusters(UWorld* World, const TMap<FGuid, FWorldPartitionActorDescView>& ActorDescViewMap, TArray<FActorCluster>& OutActorClusters, FFilterActorDescViewFunc InFilterActorDescViewFunc);
	
	// Init data
	FFilterActorDescViewFunc FilterActorDescViewFunc;

	// Generated data
	TMap<const UActorDescContainer*, TArray<FActorCluster>> Clusters;
	TArray<FActorContainerInstance> ContainerInstances;
	TArray<FActorClusterInstance> ClusterInstances;
};

#endif // #if WITH_EDITOR
