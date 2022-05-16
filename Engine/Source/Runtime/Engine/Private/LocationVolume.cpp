// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocationVolume.h"
#include "EngineDefines.h"
#include "Components/BrushComponent.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterActor.h"

ALocationVolume::ALocationVolume(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	GetBrushComponent()->SetGenerateOverlapEvents(false);

#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

#if WITH_EDITOR
	if (!IsTemplate() && GetWorld() && GetWorld()->GetWorldPartition())
	{
		WorldPartitionActorLoader = new FLoaderAdapterActor(this);
	}
#endif
}

void ALocationVolume::BeginDestroy()
{
#if WITH_EDITOR
	if (WorldPartitionActorLoader)
	{
		delete WorldPartitionActorLoader;
		WorldPartitionActorLoader = nullptr;
	}
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
IWorldPartitionActorLoaderInterface::ILoaderAdapter* ALocationVolume::GetLoaderAdapter()
{
	return WorldPartitionActorLoader;
}
#endif