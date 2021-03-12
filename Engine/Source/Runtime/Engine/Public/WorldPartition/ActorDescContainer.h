// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "WorldPartition/ActorDescList.h"
#include "ActorDescContainer.generated.h"

UCLASS()
class ENGINE_API UActorDescContainer : public UObject, public FActorDescList
{
	GENERATED_UCLASS_BODY()

	friend struct FWorldPartitionHandleUtils;
	friend class FWorldPartitionActorDesc;

public:
	void Initialize(UWorld* World, FName InPackageName, bool bRegisterDelegates);
	virtual void Uninitialize();
	
	virtual UWorld* GetWorld() const override;

#if WITH_EDITOR
	// Events
	virtual void OnObjectPreSave(UObject* Object);
	virtual void OnPackageDeleted(UPackage* Package);

	FName GetContainerPackage() const { return ContainerPackageName; }
#endif

	UPROPERTY(Transient)
	TObjectPtr<UWorld> World;

protected:
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

#if WITH_EDITOR
	TUniquePtr<FWorldPartitionActorDesc> GetActorDescriptor(const FAssetData& InAssetData);
	
	virtual void RegisterDelegates();
	virtual void UnregisterDelegates();

	virtual void OnActorDescAdded(const TUniquePtr<FWorldPartitionActorDesc>& NewActorDesc) {}
	virtual void OnActorDescRemoved(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}
	virtual void OnActorDescUpdating(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}
	virtual void OnActorDescUpdated(const TUniquePtr<FWorldPartitionActorDesc>& ActorDesc) {}

	virtual void OnActorDescRegistered(const FWorldPartitionActorDesc&) {}
	virtual void OnActorDescUnregistered(const FWorldPartitionActorDesc&) {}

	bool ShouldHandleActorEvent(const AActor* Actor);

	bool bContainerInitialized;

	FName ContainerPackageName;
#endif
};