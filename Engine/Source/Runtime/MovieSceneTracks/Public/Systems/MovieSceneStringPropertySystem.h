// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Systems/MovieScenePropertySystem.h"
#include "MovieSceneStringPropertySystem.generated.h"



UCLASS()
class MOVIESCENETRACKS_API UMovieSceneStringPropertySystem : public UMovieScenePropertySystem
{
	GENERATED_BODY()

	UMovieSceneStringPropertySystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
};
