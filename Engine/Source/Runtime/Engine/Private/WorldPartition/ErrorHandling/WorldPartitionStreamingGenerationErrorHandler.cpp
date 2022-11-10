// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"
#include "UObject/SoftObjectPath.h"

// Helpers
FString IStreamingGenerationErrorHandler::GetFullActorName(const FWorldPartitionActorDescView& ActorDescView)
{
	const FString ActorPath = ActorDescView.GetActorSoftPath().GetLongPackageName();
	const FString ActorLabel = ActorDescView.GetActorLabelOrName().ToString();
	const FString ActorPackage = ActorDescView.GetActorPackage().ToString();
	return ActorPath + TEXT(".") + ActorLabel + TEXT(" (") + ActorPackage + TEXT(")");
}
#endif