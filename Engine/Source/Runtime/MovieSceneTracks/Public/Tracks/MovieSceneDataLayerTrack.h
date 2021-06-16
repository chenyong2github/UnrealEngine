// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MovieSceneNameableTrack.h"
#include "MovieSceneDataLayerTrack.generated.h"

/**
 * A track that controls loading, unloading and visibility of data layers
 */
UCLASS( MinimalAPI )
class UMovieSceneDataLayerTrack : public UMovieSceneNameableTrack
{
public:
	GENERATED_BODY()

	UMovieSceneDataLayerTrack(const FObjectInitializer& ObjectInitializer);

	/** UMovieSceneTrack interface */
	virtual bool IsEmpty() const override;
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual void RemoveSection( UMovieSceneSection& Section ) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual bool SupportsMultipleRows() const override { return true; }

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

private:

	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
