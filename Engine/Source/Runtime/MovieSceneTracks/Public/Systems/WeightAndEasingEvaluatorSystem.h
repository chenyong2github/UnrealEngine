// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/SortedMap.h"
#include "Containers/Set.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/EntityAllocationIterator.h"

#include "WeightAndEasingEvaluatorSystem.generated.h"

class UMovieSceneEvalTimeSystem;

/**
 * System that is responsible for evaluating ease in/out factors.
 */
UCLASS()
class MOVIESCENETRACKS_API UWeightAndEasingEvaluatorSystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UWeightAndEasingEvaluatorSystem(const FObjectInitializer& ObjInit);

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;
};

