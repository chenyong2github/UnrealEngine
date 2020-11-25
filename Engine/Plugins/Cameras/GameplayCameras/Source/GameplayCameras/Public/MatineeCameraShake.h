// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Evaluation/MovieSceneCameraShakeTemplate.h"
#include "MatineeCameraShake.generated.h"

/** Custom sequencer evaluation code for Matinee camera shakes */
UCLASS()
class UMovieSceneMatineeCameraShakeEvaluator : public UMovieSceneCameraShakeEvaluator
{
	GENERATED_BODY()

	UMovieSceneMatineeCameraShakeEvaluator(const FObjectInitializer& ObjInit);

	static UMovieSceneCameraShakeEvaluator* BuildMatineeShakeEvaluator(UCameraShakeBase* ShakeInstance);

	virtual bool Setup(const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, UCameraShakeBase* ShakeInstance) override;
	virtual bool Evaluate(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player, UCameraShakeBase* ShakeInstance) override;
};

