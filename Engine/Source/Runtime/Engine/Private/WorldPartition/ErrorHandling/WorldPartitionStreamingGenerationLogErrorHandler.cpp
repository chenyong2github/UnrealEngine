// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationLogErrorHandler.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "GameFramework/Actor.h"

void FStreamingGenerationLogErrorHandler::OnInvalidReference(const FWorldPartitionActorDescView& ActorDescView, const FGuid& ReferenceGuid)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Actor %s have missing reference to %s"), *ActorDescView.GetActorLabelOrName().ToString(), *ReferenceGuid.ToString());
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceGridPlacement(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
{
	const FString StreamedActor(TEXT("Streamed actor"));
	const FString AlwaysLoadedActor(TEXT("Always loaded actor"));
	const bool bIsActorDescAlwaysLoaded = ActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;
	const bool bIsActorDescRefAlwaysLoaded = ReferenceActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;
	UE_LOG(LogWorldPartition, Log, TEXT("%s %s reference %s %s"), bIsActorDescAlwaysLoaded ? *AlwaysLoadedActor : *StreamedActor, *ActorDescView.GetActorLabelOrName().ToString(), bIsActorDescRefAlwaysLoaded ? *AlwaysLoadedActor : *StreamedActor, *ReferenceActorDescView.GetActorLabelOrName().ToString());
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceDataLayers(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Actor %s references an actor in a different set of data layers %s"), *ActorDescView.GetActorLabelOrName().ToString(), *ReferenceActorDescView.GetActorLabelOrName().ToString());
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceLevelScriptStreamed(const FWorldPartitionActorDescView& ActorDescView)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Level Script Blueprint references streamed actor %s"), *ActorDescView.GetActorLabelOrName().ToString());
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceLevelScriptDataLayers(const FWorldPartitionActorDescView& ActorDescView)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Level Script Blueprint references streamed actor %s with a non empty set of data layers"), *ActorDescView.GetActorLabelOrName().ToString());
}
#endif
