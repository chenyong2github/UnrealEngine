// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneBlenderSystem.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "Systems/MovieSceneBlenderSystemHelper.h"
#include "MovieScenePiecewiseByteBlenderSystem.generated.h"


UCLASS()
class MOVIESCENETRACKS_API UMovieScenePiecewiseByteBlenderSystem : public UMovieSceneBlenderSystem
{
public:

	GENERATED_BODY()

	UMovieScenePiecewiseByteBlenderSystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

private:

	UE::MovieScene::TSimpleBlenderSystemImpl<uint8> Impl;
};

