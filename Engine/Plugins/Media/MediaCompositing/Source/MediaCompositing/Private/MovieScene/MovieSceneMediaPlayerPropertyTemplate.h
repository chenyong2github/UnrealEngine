// Copyright 1998-2020 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Evaluation/MovieScenePropertyTemplate.h"

#include "MovieSceneMediaPlayerPropertyTemplate.generated.h"

class UMediaSource;
class UMovieSceneMediaPlayerPropertyTrack;
class UMovieSceneMediaPlayerPropertySection;


USTRUCT()
struct FMovieSceneMediaPlayerPropertySectionTemplate
	: public FMovieScenePropertySectionTemplate
{
	GENERATED_BODY()

	/** Default constructor. */
	FMovieSceneMediaPlayerPropertySectionTemplate() { }
	FMovieSceneMediaPlayerPropertySectionTemplate(const UMovieSceneMediaPlayerPropertySection* InSection, const UMovieSceneMediaPlayerPropertyTrack* InTrack);

public:

	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void SetupOverrides() override { EnableOverrides(RequiresSetupFlag); }

private:

	UPROPERTY()
	UMediaSource* MediaSource;

	UPROPERTY()
	FFrameNumber SectionStartFrame;

	UPROPERTY()
	bool bLoop;
};
