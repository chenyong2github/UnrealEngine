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
	virtual void OnObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext);
	virtual void OnPackageDeleted(UPackage* Package);
	virtual void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewObjectMap);

	FName GetContainerPackage() const { return ContainerPackageName; }

	/** Removes an actor desc without the need to load a package */
	virtual void RemoveActor(const FGuid& ActorGuid);

	void LoadAllActors(TArray<FWorldPartitionReference>& OutReferences);

	/** Instancing support */
	virtual bool GetInstancingContext(const FLinkerInstancingContext*& OutInstancingContext) const { return false; }
#endif

	virtual const FTransform& GetInstanceTransform() const { return FTransform::Identity; }

#if WITH_EDITOR
public:
	DECLARE_EVENT_OneParam(UWorldPartition, FActorDescAddedEvent, FWorldPartitionActorDesc*);
	FActorDescAddedEvent OnActorDescAddedEvent;
	
	DECLARE_EVENT_OneParam(UWorldPartition, FActorDescRemovedEvent, FWorldPartitionActorDesc*);
	FActorDescRemovedEvent OnActorDescRemovedEvent;

	DECLARE_MULTICAST_DELEGATE_OneParam(FActorDescContainerInitializeDelegate, UActorDescContainer*);
	static FActorDescContainerInitializeDelegate OnActorDescContainerInitialized;
#endif

	UPROPERTY(Transient)
	TObjectPtr<UWorld> World;

protected:
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

#if WITH_EDITOR
	//~ Begin FActorDescList Interface
	virtual void AddActorDescriptor(FWorldPartitionActorDesc* ActorDesc) override;
	virtual void RemoveActorDescriptor(FWorldPartitionActorDesc* ActorDesc) override;
	//~ End FActorDescList Interface

	// Events
	void OnWorldRenamed(UWorld* RenamedWorld);
	virtual void OnWorldRenamed();

	virtual void OnActorDescAdded(FWorldPartitionActorDesc* NewActorDesc);
	virtual void OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc);
	virtual void OnActorDescUpdating(FWorldPartitionActorDesc* ActorDesc) {}
	virtual void OnActorDescUpdated(FWorldPartitionActorDesc* ActorDesc) {}

	bool ShouldHandleActorEvent(const AActor* Actor);

	bool bContainerInitialized;

	FName ContainerPackageName;
#endif

private:
#if WITH_EDITOR
	bool ShouldRegisterDelegates();
	void RegisterEditorDelegates();
	void UnregisterEditorDelegates();
#endif
};