// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationLogErrorHandler.h"
#include "WorldPartition/WorldPartitionLog.h"

void FStreamingGenerationLogErrorHandler::OnInvalidReference(const FWorldPartitionActorDescView& ActorDescView, const FGuid& ReferenceGuid)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Actor %s have missing reference to %s"), *GetActorLabel(ActorDescView), *ReferenceGuid.ToString());
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceGridPlacement(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
{
	const FString StreamedActor(TEXT("Streamed actor"));
	const FString AlwaysLoadedActor(TEXT("Always loaded actor"));
	const bool bIsActorDescAlwaysLoaded = ActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;
	const bool bIsActorDescRefAlwaysLoaded = ReferenceActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;
	UE_LOG(LogWorldPartition, Log, TEXT("%s %s reference %s %s"), bIsActorDescAlwaysLoaded ? *AlwaysLoadedActor : *StreamedActor, *GetActorLabel(ActorDescView), bIsActorDescRefAlwaysLoaded ? *AlwaysLoadedActor : *StreamedActor, *GetActorLabel(ReferenceActorDescView));
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceDataLayers(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Actor %s references an actor in a different set of data layers %s"), *GetActorLabel(ActorDescView), *GetActorLabel(ReferenceActorDescView));
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceLevelScriptStreamed(const FWorldPartitionActorDescView& ActorDescView)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Level Script Blueprint references streamed actor %s"), *GetActorLabel(ActorDescView));
}

void FStreamingGenerationLogErrorHandler::OnInvalidReferenceLevelScriptDataLayers(const FWorldPartitionActorDescView& ActorDescView)
{
	UE_LOG(LogWorldPartition, Log, TEXT("Level Script Blueprint references streamed actor %s with a non empty set of data layers"), *GetActorLabel(ActorDescView));
}
#endif
