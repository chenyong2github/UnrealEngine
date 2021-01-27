// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "ActorDescContainer.generated.h"

UCLASS()
class ENGINE_API UActorDescContainer : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	void Initialize(FName InPackageName, bool bRegisterDelegates);
	virtual void Uninitialize();
	
#if WITH_EDITOR
	// Asset registry events
	virtual void OnAssetAdded(const FAssetData& InAssetData);
	virtual void OnAssetRemoved(const FAssetData& InAssetData);
	virtual void OnAssetUpdated(const FAssetData& InAssetData);
#endif

protected:
#if WITH_EDITOR
	TUniquePtr<FWorldPartitionActorDesc> GetActorDescriptor(const FAssetData& InAssetData);
	
	virtual void RegisterDelegates();
	virtual void UnregisterDelegates();

	virtual void OnActorDescAdded(const TUniquePtr<FWorldPartitionActorDesc>& NewActorDesc) {}
	virtual void OnActorDescRemoved(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}
	virtual void OnActorDescUpdating(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}
	virtual void OnActorDescUpdated(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}

	bool ShouldHandleAssetEvent(const FAssetData& InAssetData);

	bool bContainerInitialized;
	bool bIgnoreAssetRegistryEvents;

	TChunkedArray<TUniquePtr<FWorldPartitionActorDesc>> ActorDescList;

	FName ContainerPackageName;
public:
	TMap<FGuid, TUniquePtr<FWorldPartitionActorDesc>*> Actors;
	
#endif
};