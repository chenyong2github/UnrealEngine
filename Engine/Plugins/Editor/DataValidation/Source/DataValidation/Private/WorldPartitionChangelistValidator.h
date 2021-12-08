// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EditorValidatorBase.h"
#include "WorldPartition/ErrorHandling/WorldPartitionStreamingGenerationErrorHandler.h"

#include "WorldPartitionChangelistValidator.generated.h"

class UDataValidationChangelist;
class UActorDescContainer;
class FWorldPartitionActorDesc;

UCLASS()
class DATAVALIDATION_API UWorldPartitionChangelistValidator : public UEditorValidatorBase, public IStreamingGenerationErrorHandler
{
	GENERATED_BODY()

protected:		
	
	TArray<FText>*							Errors = nullptr;
	TSet<FGuid>								RelevantActorGuids;

	virtual bool CanValidateAsset_Implementation(UObject* InAsset) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors) override;

	EDataValidationResult ValidateActorsListFromChangeList(UDataValidationChangelist* Changelist);

	// Return true if this ActorDescView is pertinent to the current ChangeList
	bool Filter(const FWorldPartitionActorDescView& ActorDescView);

	// IStreamingGenerationErrorHandler Interface methods
	virtual void OnInvalidReference(const FWorldPartitionActorDescView& ActorDescView, const FGuid& ReferenceGuid) override;
	virtual void OnInvalidReferenceGridPlacement(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView) override;
	virtual void OnInvalidReferenceDataLayers(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView) override;
	virtual void OnInvalidReferenceLevelScriptStreamed(const FWorldPartitionActorDescView& ActorDescView) override;
	virtual void OnInvalidReferenceLevelScriptDataLayers(const FWorldPartitionActorDescView& ActorDescView) override;
	virtual void OnInvalidReferenceRuntimeGrid(const FWorldPartitionActorDescView& ActorDescView, const FWorldPartitionActorDescView& ReferenceActorDescView) override;
};

