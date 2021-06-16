// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"
#include "WorldPartition/DataLayer/ActorDataLayer.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "MovieSceneSection.h"
#include "MovieSceneDataLayerSection.generated.h"

UCLASS(MinimalAPI)
class UMovieSceneDataLayerSection
	: public UMovieSceneSection
	, public IMovieSceneEntityProvider
{
public:

	GENERATED_BODY()

	UMovieSceneDataLayerSection(const FObjectInitializer& ObjInit);

	UFUNCTION(BlueprintPure, Category = "Sequencer|Section")
	MOVIESCENETRACKS_API EDataLayerState GetDesiredState() const;

	UFUNCTION(BlueprintCallable, Category = "Sequencer|Section")
	MOVIESCENETRACKS_API void SetDesiredState(EDataLayerState InDesiredState);

	UFUNCTION(BlueprintPure, Category = "Sequencer|Section")
	const TArray<FActorDataLayer>& GetDataLayers() const { return DataLayers; }

	UFUNCTION(BlueprintCallable, Category = "Sequencer|Section")
	void SetDataLayers(const TArray<FActorDataLayer>& InDataLayers) { DataLayers = InDataLayers; }

private:

	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) override;

private:

	/** A list of data layers that should be loaded or unloaded by this section */
	UPROPERTY(EditAnywhere, Category=DataLayer)
	TArray<FActorDataLayer> DataLayers;

	/** Whether or not the levels in this section should be visible or hidden. */
	UPROPERTY(EditAnywhere, Category=DataLayer)
	EDataLayerState DesiredState;
};
