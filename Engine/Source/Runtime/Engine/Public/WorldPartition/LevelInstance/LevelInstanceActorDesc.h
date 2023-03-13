// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"
#include "Containers/Map.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/Filter/WorldPartitionActorFilter.h"

class ULevelInstanceSubsystem;
class UActorDescContainer;
class IStreamingGenerationErrorHandler;
enum class ELevelInstanceRuntimeBehavior : uint8;

/**
 * ActorDesc for Actors that are part of a LevelInstanceActor Level.
 */
class ENGINE_API FLevelInstanceActorDesc : public FWorldPartitionActorDesc
{
public:
	FLevelInstanceActorDesc();
	virtual ~FLevelInstanceActorDesc() override;

	virtual bool IsContainerInstance() const override;
	virtual FName GetLevelPackage() const override { return LevelPackage; }
	virtual bool GetContainerInstance(FContainerInstance& OutContainerInstance, bool bInBuildFilter) const override;
	virtual const FWorldPartitionActorFilter* GetContainerFilter() const override { return &Filter; }
	virtual void CheckForErrors(IStreamingGenerationErrorHandler* ErrorHandler) const override;

protected:
	virtual void Init(const AActor* InActor) override;
	virtual void Init(const FWorldPartitionActorDescInitData& DescData) override;
	virtual bool Equals(const FWorldPartitionActorDesc* Other) const override;
	virtual void TransferFrom(const FWorldPartitionActorDesc* From) override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void SetContainer(UActorDescContainer* InContainer, UWorld* InWorldContext) override;

	FName LevelPackage;
	FTransform LevelInstanceTransform;
	ELevelInstanceRuntimeBehavior DesiredRuntimeBehavior;

	TWeakObjectPtr<UActorDescContainer> LevelInstanceContainer;
	TWeakObjectPtr<UWorld> LevelInstanceContainerWorldContext;

	FWorldPartitionActorFilter Filter;

private:
	void RegisterContainerInstance(UWorld* InWorldContext);
	void UnregisterContainerInstance();
	void UpdateBounds();
};
#endif
