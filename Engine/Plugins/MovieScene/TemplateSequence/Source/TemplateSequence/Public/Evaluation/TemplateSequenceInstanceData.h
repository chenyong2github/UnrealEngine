// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Evaluation/MovieSceneSequenceInstanceData.h"
#include "Evaluation/MovieSceneEvaluationOperand.h"
#include "TemplateSequenceInstanceData.generated.h"

USTRUCT()
struct FTemplateSequenceInstanceData : public FMovieSceneSequenceInstanceData
{
	GENERATED_BODY()

	UPROPERTY()
	FMovieSceneEvaluationOperand Operand;

private:

	virtual UScriptStruct& GetScriptStructImpl() const { return *StaticStruct(); }
};
