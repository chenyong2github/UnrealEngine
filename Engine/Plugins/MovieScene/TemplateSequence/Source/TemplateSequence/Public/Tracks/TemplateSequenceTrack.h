// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/InlineValue.h"
#include "Compilation/MovieSceneSegmentCompiler.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "TemplateSequenceTrack.generated.h"

class UTemplateSequence;
class UTemplateSequenceSection;
struct FMovieSceneEvaluationTrack;

UCLASS(MinimalAPI)
class UTemplateSequenceTrack : public UMovieSceneSubTrack
{
public:

	GENERATED_BODY()

	virtual UMovieSceneSection* AddNewTemplateSequenceSection(FFrameNumber KeyTime, UTemplateSequence* InSequence);

	// UMovieSceneTrack interface
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual void PostCompile(FMovieSceneEvaluationTrack& OutTrack, const FMovieSceneTrackCompilerArgs& Args) const override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDisplayName() const override;
#endif

private:
	static uint16 GetEvaluationPriority() { return uint16(0x1000); }  // One more than spawn tracks.
};
