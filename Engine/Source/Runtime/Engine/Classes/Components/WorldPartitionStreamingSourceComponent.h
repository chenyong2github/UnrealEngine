// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/ActorComponent.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartitionStreamingSourceComponent.generated.h"

UCLASS(Meta = (BlueprintSpawnableComponent))
class ENGINE_API UWorldPartitionStreamingSourceComponent : public UActorComponent, public IWorldPartitionStreamingSourceProvider
{
	GENERATED_UCLASS_BODY()

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	// IWorldPartitionStreamingSourceProvider interface
	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) override;
};