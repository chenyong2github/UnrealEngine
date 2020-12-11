// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "UObject/ObjectMacros.h"

#include "Evaluation/PersistentEvaluationData.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Evaluation/MovieSceneCameraShakeTemplate.h"
#include "Sections/MovieSceneCameraAnimSection.h"
#include "Sections/MovieSceneCameraShakeSection.h"

#include "MovieSceneCameraAnimTemplate.generated.h"

class ACameraActor;

/** Persistent data for logic that requires a temporary camera actor */
struct MOVIESCENETRACKS_API FMovieSceneMatineeCameraData : IPersistentEvaluationData
{
	static FMovieSceneSharedDataId GetSharedDataID();
	static FMovieSceneMatineeCameraData& Get(const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData);

	ACameraActor* GetTempCameraActor(IMovieScenePlayer& Player);

protected:
	TWeakObjectPtr<ACameraActor> TempCameraActor;
};

/** Section template for a camera anim */
USTRUCT()
struct FMovieSceneCameraAnimSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()
	
	FMovieSceneCameraAnimSectionTemplate() {}
	FMovieSceneCameraAnimSectionTemplate(const UMovieSceneCameraAnimSection& Section);

private:
	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

	/** Source data taken from the section */
	UPROPERTY()
	FMovieSceneCameraAnimSectionData SourceData;

	/** Cached section start time */
	UPROPERTY()
	FFrameNumber SectionStartTime;

	friend struct FAccumulateCameraAnimExecutionToken;
};

