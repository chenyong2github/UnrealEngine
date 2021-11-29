// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationMapCheckErrorHandler.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"

#define LOCTEXT_NAMESPACE "WorldPartition"

void FStreamingGenerationMapCheckErrorHandler::OnInvalidReference(const FWorldPartitionActorDescView& ActorDescView, const FGuid& ReferenceGuid)
{
	TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Warning()
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_Actor", "Actor")))
		->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_HaveMissingRefsTo", "have missing references to")))
		->AddToken(FTextToken::Create(FText::FromString(ReferenceGuid.ToString())))
		->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_MissingActorReference_CheckForErrors"))));
}

void FStreamingGenerationMapCheckErrorHandler::OnInvalidReferenceGridPlacement(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
{
	const bool bIsActorDescAlwaysLoaded = ActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;
	const bool bIsActorDescRefAlwaysLoaded = ReferenceActorDescView.GetGridPlacement() == EActorGridPlacement::AlwaysLoaded;

	const FText StreamedActor(LOCTEXT("MapCheck_WorldPartition_StreamedActor", "Streamed actor"));
	const FText AlwaysLoadedActor(LOCTEXT("MapCheck_WorldPartition_AlwaysLoadedActor", "Always loaded actor"));

	TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
		->AddToken(FTextToken::Create(bIsActorDescAlwaysLoaded ? AlwaysLoadedActor : StreamedActor))
		->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_References", "references")))
		->AddToken(FTextToken::Create(bIsActorDescRefAlwaysLoaded ? AlwaysLoadedActor : StreamedActor)								)
		->AddToken(FAssetNameToken::Create(GetActorLabel(ReferenceActorDescView)))
		->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_StreamedActorReferenceAlwaysLoadedActor_CheckForErrors"))));
}

void FStreamingGenerationMapCheckErrorHandler::OnInvalidReferenceDataLayers(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView)
{
	TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_Actor", "Actor")))
		->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_ReferenceActorInOtherDataLayers", "references an actor in a different set of data layers")))
		->AddToken(FAssetNameToken::Create(GetActorLabel(ReferenceActorDescView)))
		->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_ActorReferenceActorInAnotherDataLayer_CheckForErrors"))));
}

void FStreamingGenerationMapCheckErrorHandler::OnInvalidReferenceLevelScriptStreamed(const FWorldPartitionActorDescView& ActorDescView)
{
	TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_LevelScriptBlueprintStreamedActorReference", "Level Script Blueprint references streamed actor")))
		->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
		->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_LevelScriptBlueprintRefefenceStreamed_CheckForErrors"))));
}

void FStreamingGenerationMapCheckErrorHandler::OnInvalidReferenceLevelScriptDataLayers(const FWorldPartitionActorDescView& ActorDescView)
{
	TSharedRef<FTokenizedMessage> Error = FMessageLog("MapCheck").Error()
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_LevelScriptBlueprintActorReference", "Level Script Blueprint references actor")))
		->AddToken(FAssetNameToken::Create(GetActorLabel(ActorDescView)))
		->AddToken(FTextToken::Create(LOCTEXT("MapCheck_WorldPartition_LevelScriptBlueprintDataLayerReference", "with a non empty set of data layers")))
		->AddToken(FMapErrorToken::Create(FName(TEXT("WorldPartition_LevelScriptBlueprintRefefenceDataLayer_CheckForErrors"))));
}

#undef LOCTEXT_NAMESPACE

#endif
