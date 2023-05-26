// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "Misc/FrameTime.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "MovieSceneEvalTimeSystem.generated.h"

class UObject;
struct FFrameTime;

namespace UE::MovieScene
{
	struct FEvaluatedTime
	{
		FFrameTime FrameTime;
		double Seconds;
	};
}

UCLASS()
class MOVIESCENE_API UMovieSceneEvalTimeSystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UMovieSceneEvalTimeSystem(const FObjectInitializer& ObjInit);

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;
	virtual void OnSchedulePersistentTasks(UE::MovieScene::IEntitySystemScheduler* TaskScheduler) override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

private:

	TArray<UE::MovieScene::FEvaluatedTime, TInlineAllocator<16>> EvaluatedTimes;

	UE::MovieScene::FEntityComponentFilter RelevantFilter;
};

