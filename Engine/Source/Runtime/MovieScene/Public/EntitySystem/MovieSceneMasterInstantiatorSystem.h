// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "EntitySystem/MovieSceneEntityIDs.h"

#include "MovieSceneMasterInstantiatorSystem.generated.h"


UCLASS()
class MOVIESCENE_API UMovieSceneMasterInstantiatorSystem : public UMovieSceneEntityInstantiatorSystem
{
	GENERATED_BODY()

public:

	UMovieSceneMasterInstantiatorSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	void InstantiateAllocation(const UE::MovieScene::FEntityAllocation* ParentAllocation);
};