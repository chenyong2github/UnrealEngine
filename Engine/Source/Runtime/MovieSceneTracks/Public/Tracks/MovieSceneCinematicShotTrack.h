// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/InlineValue.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Compilation/MovieSceneSegmentCompiler.h"
#include "MovieSceneCinematicShotTrack.generated.h"

class UMovieSceneSequence;
class UMovieSceneSubSection;

/**
 * A track that holds consecutive sub sequences.
 */
UCLASS(MinimalAPI)
class UMovieSceneCinematicShotTrack
	: public UMovieSceneSubTrack
{
	GENERATED_BODY()

public:

	UMovieSceneCinematicShotTrack(const FObjectInitializer& ObjectInitializer);

	MOVIESCENETRACKS_API void SortSections();

	// UMovieSceneSubTrack interface

	MOVIESCENETRACKS_API virtual UMovieSceneSubSection* AddSequence(UMovieSceneSequence* Sequence, FFrameNumber StartTime, int32 Duration) { return AddSequenceOnRow(Sequence, StartTime, Duration, INDEX_NONE); }
	MOVIESCENETRACKS_API virtual UMovieSceneSubSection* AddSequenceOnRow(UMovieSceneSequence* Sequence, FFrameNumber StartTime, int32 Duration, int32 RowIndex);

	// UMovieSceneTrack interface

	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;
	virtual bool SupportsMultipleRows() const override;
	virtual bool PopulateEvaluationTree(TMovieSceneEvaluationTree<FMovieSceneTrackEvaluationData>& OutData) const override;
	virtual int8 GetEvaluationFieldVersion() const override;
	
#if WITH_EDITOR
	virtual EMovieSceneSectionMovedResult OnSectionMoved(UMovieSceneSection& Section, const FMovieSceneSectionMovedParams& Params) override;
#endif

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif
};
