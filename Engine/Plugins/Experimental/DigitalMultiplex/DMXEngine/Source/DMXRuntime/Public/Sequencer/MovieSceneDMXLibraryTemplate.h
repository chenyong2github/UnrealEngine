// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sequencer/MovieSceneDMXLibrarySection.h"

#include "Evaluation/MovieSceneEvalTemplate.h"

#include "MovieSceneDMXLibraryTemplate.generated.h"

class UDMXLibrary;



/** Template that performs evaluation of Fixture Patch sections */
USTRUCT()
struct FMovieSceneDMXLibraryTemplate
	: public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	/**  */
	FMovieSceneDMXLibraryTemplate() : Section(nullptr) {}
	FMovieSceneDMXLibraryTemplate(const UMovieSceneDMXLibrarySection& InSection);

private:

	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

private:

	UPROPERTY()
	const UMovieSceneDMXLibrarySection* Section;
};