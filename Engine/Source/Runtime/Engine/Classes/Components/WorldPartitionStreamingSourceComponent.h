// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/ActorComponent.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartitionStreamingSourceComponent.generated.h"

UCLASS(Meta = (BlueprintSpawnableComponent), HideCategories = (Tags, Sockets, ComponentTick, ComponentReplication, Activation, Cooking, Events, AssetUserData, Collision))
class ENGINE_API UWorldPartitionStreamingSourceComponent : public UActorComponent, public IWorldPartitionStreamingSourceProvider
{
	GENERATED_UCLASS_BODY()

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Enable the component */
	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void EnableStreamingSource() { bStreamingSourceEnabled = true; }

	/** Disable the component */
	UFUNCTION(BlueprintCallable, Category = "Streaming")
	void DisableStreamingSource() { bStreamingSourceEnabled = false; }

	/** Returns true if the component is active. */
	UFUNCTION(BlueprintPure, Category = "Streaming")
	bool IsStreamingSourceEnabled() const { return bStreamingSourceEnabled; }

	// IWorldPartitionStreamingSourceProvider interface
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) override;

private:
	/** Whether this component is enabled or not */
	UPROPERTY(EditAnywhere, Category = "Streaming")
	bool bStreamingSourceEnabled;

	UPROPERTY(EditAnywhere, Category = "Streaming")
	EStreamingSourceTargetState TargetState;
};