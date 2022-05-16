// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"

#include "LocationVolume.generated.h"

class FLoaderAdapterActor;

/**
 * A volume representing a location in the world
 */
UCLASS()
class ENGINE_API ALocationVolume : public AVolume, public IWorldPartitionActorLoaderInterface
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

	//~ Begin AActor Interface
	virtual bool IsEditorOnly() const override { return !bIsRuntime; }
#if WITH_EDITOR
	virtual bool CanChangeIsSpatiallyLoadedFlag() const override { return false; }
	virtual bool SupportsDataLayer() const override { return false; }
#endif
	//~ End AActor Interface

#if WITH_EDITOR
	//~ Begin IWorldPartitionActorLoaderInterface interface
	virtual ILoaderAdapter* GetLoaderAdapter() override;
	//~ End IWorldPartitionActorLoaderInterface interface
#endif

	UPROPERTY(EditAnywhere, Category=LocationVolume)
	FColor DebugColor;

	UPROPERTY(EditAnywhere, Category=LocationVolume)
	uint8 bIsRuntime : 1;

#if WITH_EDITOR
private:
	FLoaderAdapterActor* WorldPartitionActorLoader;
#endif
};