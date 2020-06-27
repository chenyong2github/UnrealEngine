// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"

#include "MovieSceneBoundObjectInstantiator.generated.h"


UCLASS()
class MOVIESCENE_API UMovieSceneGenericBoundObjectInstantiator : public UMovieSceneEntityInstantiatorSystem
{
	GENERATED_BODY()

	UMovieSceneGenericBoundObjectInstantiator(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;
};

