// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieSceneDecompositionQuery.h"
#include "Systems/MovieScenePiecewiseBlenderSystemImpl.h"

#include "MovieScenePiecewiseFloatBlenderSystem.generated.h"


UCLASS()
class MOVIESCENETRACKS_API UMovieScenePiecewiseFloatBlenderSystem : public UMovieSceneBlenderSystem, public IMovieSceneValueDecomposer
{
public:
	GENERATED_BODY()

	UMovieScenePiecewiseFloatBlenderSystem(const FObjectInitializer& ObjInit);

	using FMovieSceneEntityID  = UE::MovieScene::FMovieSceneEntityID;
	using FComponentTypeID     = UE::MovieScene::FComponentTypeID;

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	virtual FGraphEventRef DispatchDecomposeTask(const UE::MovieScene::FValueDecompositionParams& Params, UE::MovieScene::FAlignedDecomposedValue* Output) override;

private:

	UE::MovieScene::TPiecewiseBlenderSystemImpl<float> Impl;
};

