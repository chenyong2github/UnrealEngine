// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Sections/MovieSceneSubSection.h"
#include "TemplateSequenceSection.generated.h"

UCLASS(MinimalAPI)
class UTemplateSequenceSection : public UMovieSceneSubSection
{
public:

	GENERATED_BODY()

	UTemplateSequenceSection();

	// UMovieSceneSubSection interface
	virtual FMovieSceneSubSequenceData GenerateSubSequenceData(const FSubSequenceInstanceDataParams& Params) const override;
	virtual FMovieSceneEvalTemplatePtr GenerateTemplate() const override;

	// UMovieSceneSection interface
	virtual void OnDilated(float DilationFactor, FFrameNumber Origin) override;
};
