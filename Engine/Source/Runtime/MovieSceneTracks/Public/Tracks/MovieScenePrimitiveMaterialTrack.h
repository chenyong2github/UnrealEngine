// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Compilation/IMovieSceneTrackTemplateProducer.h"
#include "MovieScenePrimitiveMaterialTrack.generated.h"

UCLASS(MinimalAPI)
class UMovieScenePrimitiveMaterialTrack : public UMovieScenePropertyTrack, public IMovieSceneTrackTemplateProducer
{
public:

	GENERATED_BODY()

	UMovieScenePrimitiveMaterialTrack(const FObjectInitializer& ObjInit);

	/* Set the material index that this track is assigned to */
	MOVIESCENETRACKS_API void SetMaterialIndex(int32 MaterialIndex);

	/* Get the material index that this track is assigned to */
	MOVIESCENETRACKS_API int32 GetMaterialIndex() const;

	/*~ UMovieSceneTrack interface */
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override;
	virtual void PostCompile(FMovieSceneEvaluationTrack& OutTrack, const FMovieSceneTrackCompilerArgs& Args) const override;

private:
	UPROPERTY()
	int32 MaterialIndex;
};
