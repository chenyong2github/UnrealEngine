// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/IMovieScenePropertyComponentHandler.h"
#include "Systems/MovieScenePropertySystem.h"
#include "MovieSceneVectorPropertySystem.generated.h"

UCLASS()
class MOVIESCENETRACKS_API UMovieSceneVectorPropertySystem : public UMovieScenePropertySystem
{
	GENERATED_BODY()

private:
	UMovieSceneVectorPropertySystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
};
