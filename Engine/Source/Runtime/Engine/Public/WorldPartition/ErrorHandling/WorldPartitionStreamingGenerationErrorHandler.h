// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "WorldPartition/WorldPartitionActorDescView.h"

class ENGINE_API IStreamingGenerationErrorHandler
{
public:
	virtual ~IStreamingGenerationErrorHandler() {}

	/** 
	 * Called when an actor references an invalid actor.
	 */
	virtual void OnInvalidReference(const FWorldPartitionActorDescView& ActorDescView, const FGuid& ReferenceGuid) = 0;

	/** 
	 * Called when an actor references an actor using a different grid placement.
	 */
	virtual void OnInvalidReferenceGridPlacement(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView) = 0;

	/** 
	 * Called when an actor references an actor using a different set of data layers.
	 */
	virtual void OnInvalidReferenceDataLayers(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView) = 0;

	/**
	 * Called when an actor references an actor using a different RuntimeGrid.
	 */
	virtual void OnInvalidReferenceRuntimeGrid(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView) = 0;

	/** 
	 * Called when the level script references a streamed actor.
	 */
	virtual void OnInvalidReferenceLevelScriptStreamed(const FWorldPartitionActorDescView& ActorDescView) = 0;

	/** 
	 * Called when an actor descriptor references an actor using data layers.
	 */
	virtual void OnInvalidReferenceLevelScriptDataLayers(const FWorldPartitionActorDescView& ActorDescView) = 0;
};
#endif
