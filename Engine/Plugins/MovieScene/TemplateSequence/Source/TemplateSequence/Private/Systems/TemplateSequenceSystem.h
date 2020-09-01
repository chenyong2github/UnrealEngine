// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneObjectBindingID.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "Evaluation/MovieSceneEvaluationOperand.h"
#include "UObject/ObjectMacros.h"
#include "TemplateSequenceSystem.generated.h"

namespace UE
{
namespace MovieScene
{

struct FTemplateSequenceComponentData
{
	FMovieSceneEvaluationOperand InnerOperand;
};

struct FTemplateSequenceComponentTypes
{
public:
	static FTemplateSequenceComponentTypes* Get();

	TComponentTypeID<FTemplateSequenceComponentData> TemplateSequence;

private:
	FTemplateSequenceComponentTypes();
};

} // namespace MovieScene
} // namespace UE

UCLASS(MinimalAPI)
class UTemplateSequenceSystem : public UMovieSceneEntitySystem
{
public:
	GENERATED_BODY()

	UTemplateSequenceSystem(const FObjectInitializer& ObjInit);

private:
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

private:
	UE::MovieScene::FCachedEntityFilterResult_Match ApplicableFilter;
};
