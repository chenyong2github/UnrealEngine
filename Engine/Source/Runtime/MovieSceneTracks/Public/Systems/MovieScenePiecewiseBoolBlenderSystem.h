// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "Systems/MovieSceneBlenderSystemHelper.h"
#include "MovieScenePiecewiseBoolBlenderSystem.generated.h"


UCLASS()
class MOVIESCENETRACKS_API UMovieScenePiecewiseBoolBlenderSystem : public UMovieSceneBlenderSystem
{
public:

	GENERATED_BODY()

	UMovieScenePiecewiseBoolBlenderSystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

private:

	UE::MovieScene::TSimpleBlenderSystemImpl<bool> Impl;
};

