// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntityIDs.h"

#include "MovieSceneEvalTimeSystem.generated.h"


struct FFrameTime;

UCLASS()
class MOVIESCENE_API UMovieSceneEvalTimeSystem : public UMovieSceneEntitySystem
{
public:
	GENERATED_BODY()

	UMovieSceneEvalTimeSystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

private:
	TArray<FFrameTime> FrameTimes;
};

