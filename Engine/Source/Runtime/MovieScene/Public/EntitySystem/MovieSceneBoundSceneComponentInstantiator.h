// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"

#include "MovieSceneBoundSceneComponentInstantiator.generated.h"

UCLASS()
class MOVIESCENE_API UMovieSceneBoundSceneComponentInstantiator : public UMovieSceneEntityInstantiatorSystem
{
	GENERATED_BODY()

	UMovieSceneBoundSceneComponentInstantiator(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;
};

/**
 * Empty interface that indicates that an object is able to "impersonate" a USceneComponent
 * for the purposes of Sequencer animation.
 *
 * This is useful when we override a sequence's object bindings with some other object that doesn't
 * have the same strongly-typed components.  Most component bindings will just be animated in a 
 * duck-typing kind of way (looking up properties by name), but scene components are "special",
 * and therefore require this interface to get around if needed.
 */
UINTERFACE()
class UMovieSceneSceneComponentImpersonator : public UInterface
{
	GENERATED_BODY()
};

class IMovieSceneSceneComponentImpersonator
{
	GENERATED_BODY()
};
