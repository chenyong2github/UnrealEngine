// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/ActorDescList.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "ActorDescContainer.generated.h"

UCLASS()
class ENGINE_API UActorDescContainer : public UObject, public FActorDescList
{
	GENERATED_UCLASS_BODY()

	friend struct FWorldPartitionHandleUtils;
	friend class FWorldPartitionActorDesc;

public:
	void Initialize(UWorld* World, FName InPackageName);
	virtual void Uninitialize();
	
	virtual UWorld* GetWorld() const override;

#if WITH_EDITOR
	// Events
	virtual void OnObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext);
	virtual void OnPackageDeleted(UPackage* Package);
	virtual void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewObjectMap);

	FName GetContainerPackage() const { return ContainerPackageName; }

	/** Removes an actor desc without the need to load a package */
	virtual void RemoveActor(const FGuid& ActorGuid);

	void PinActor(const FGuid& ActorGuid);
	void UnpinActor(const FGuid& ActorGuid);
	bool IsActorPinned(const FGuid& ActorGuid) const { return PinnedActors.Contains(ActorGuid); }
public:
	DECLARE_EVENT_OneParam(UWorldPartition, FActorDescAddedEvent, FWorldPartitionActorDesc*);
	FActorDescAddedEvent OnActorDescAddedEvent;
	
	DECLARE_EVENT_OneParam(UWorldPartition, FActorDescRemovedEvent, FWorldPartitionActorDesc*);
	FActorDescRemovedEvent OnActorDescRemovedEvent;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UWorld> World;

protected:
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

#if WITH_EDITOR
	virtual void OnActorDescAdded(FWorldPartitionActorDesc* NewActorDesc);
	virtual void OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc);
	virtual void OnActorDescUpdating(FWorldPartitionActorDesc* ActorDesc) {}
	virtual void OnActorDescUpdated(FWorldPartitionActorDesc* ActorDesc) {}

	virtual void OnActorDescRegistered(const FWorldPartitionActorDesc&) {}
	virtual void OnActorDescUnregistered(const FWorldPartitionActorDesc&) {}

	bool ShouldHandleActorEvent(const AActor* Actor);

	TMap<FGuid, FWorldPartitionReference> PinnedActors;
	TMap<FGuid, TMap<FGuid, FWorldPartitionReference>> PinnedActorRefs;
	
	bool bContainerInitialized;

	FName ContainerPackageName;
#endif

private:
#if WITH_EDITOR
	void RegisterEditorDelegates();
	void UnregisterEditorDelegates();
#endif
};