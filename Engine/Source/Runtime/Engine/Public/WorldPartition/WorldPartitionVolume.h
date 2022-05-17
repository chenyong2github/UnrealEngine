// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartitionVolume.generated.h"

class FLoaderAdapterActor;

UCLASS(Deprecated, meta = (DeprecationMessage = "WorldPartitionVolume has been replaced by LocationVolume"))
class ENGINE_API ADEPRECATED_WorldPartitionVolume : public AVolume
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin AActor Interface
	virtual bool IsEditorOnly() const override { return true; }
#if WITH_EDITOR
	virtual bool CanChangeIsSpatiallyLoadedFlag() const override { return false; }
	virtual bool SupportsDataLayer() const override { return false; }
#endif
	//~ End AActor Interface
};