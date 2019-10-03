// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "TemplateSequenceSectionTemplate.generated.h"

class UTemplateSequenceSection;

USTRUCT()
struct FTemplateSequenceSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	FTemplateSequenceSectionTemplate();
	FTemplateSequenceSectionTemplate(const UTemplateSequenceSection& Section);

	// FMovieSceneEvalTemplate interface.
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void SetupOverrides() override;
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;
	virtual void Initialize(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
	virtual void TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;

private:

	UPROPERTY()
	FFrameNumber SectionStartTime;

	UPROPERTY()
	FMovieSceneEvaluationOperand InnerOperand;
};
