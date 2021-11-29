// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"

FString IStreamingGenerationErrorHandler::GetActorLabel(const FWorldPartitionActorDescView& ActorDescView)
{
	const FName ActorLabel = ActorDescView.GetActorLabel();
	if (!ActorLabel.IsNone())
	{
		return ActorLabel.ToString();
	}

	const FString ActorPath = ActorDescView.GetActorPath().ToString();

	FString SubObjectName;
	FString SubObjectContext;
	if (FString(ActorPath).Split(TEXT("."), &SubObjectContext, &SubObjectName))
	{
		return SubObjectName;
	}

	return ActorPath;
};

#endif
