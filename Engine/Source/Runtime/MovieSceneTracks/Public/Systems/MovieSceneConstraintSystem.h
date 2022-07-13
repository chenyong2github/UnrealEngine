// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "MovieSceneConstraintSystem.generated.h"

/**
 * System that is responsible for propagating constraints to a bound object's FConstraintsManagerController.
 */
UCLASS()
class MOVIESCENETRACKS_API UMovieSceneConstraintSystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UMovieSceneConstraintSystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
};
