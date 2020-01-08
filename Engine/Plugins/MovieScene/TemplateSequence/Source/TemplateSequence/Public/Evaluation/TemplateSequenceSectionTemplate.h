// Copyright Epic Games, Inc. All Rights Reserved.

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
	virtual void Setup(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;
	virtual void TearDown(FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) const override;

private:
	FMovieSceneObjectBindingID GetAbsoluteInnerBindingID(FMovieSceneSequenceID LocalSequenceID, IMovieScenePlayer& Player) const;

	UPROPERTY()
	FFrameNumber SectionStartTime;

	UPROPERTY()
	FGuid OuterBindingId;

	UPROPERTY()
	FMovieSceneEvaluationOperand InnerOperand;

	friend class UTemplateSequenceTrack;
};
