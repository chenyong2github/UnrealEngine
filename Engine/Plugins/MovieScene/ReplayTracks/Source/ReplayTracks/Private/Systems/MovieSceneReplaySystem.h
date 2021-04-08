// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneObjectBindingID.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "Evaluation/MovieSceneEvaluationOperand.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ObjectMacros.h"
#include "MovieSceneReplaySystem.generated.h"

class UMovieSceneReplaySection;

namespace UE
{
namespace MovieScene
{

struct FReplayComponentData
{
	FReplayComponentData() : Section(nullptr) {}
	FReplayComponentData(const UMovieSceneReplaySection* InSection) : Section(InSection) {}

	const UMovieSceneReplaySection* Section = nullptr;
};

struct FReplayComponentTypes
{
public:
	static FReplayComponentTypes* Get();

	TComponentTypeID<FReplayComponentData> Replay;

private:
	FReplayComponentTypes();
};

} // namespace MovieScene
} // namespace UE

UCLASS(MinimalAPI)
class UMovieSceneReplaySystem : public UMovieSceneEntitySystem
{
public:
	GENERATED_BODY()

	UMovieSceneReplaySystem(const FObjectInitializer& ObjInit);

private:
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	void OnRunInstantiation();
	void OnRunEvaluation();
	void OnRunFinalization();

	static void OnPreLoadMap(const FString& MapName, IMovieScenePlayer* Player);
	static void OnPostLoadMap(UWorld* World, IMovieScenePlayer* LastPlayer, FMovieSceneContext LastContext);
	static void OnReplayStarted(UWorld* World, IMovieScenePlayer* LastPlayer, FMovieSceneContext LastContext);

private:

	struct FReplayInfo
	{
		const UMovieSceneReplaySection* Section = nullptr;
		UE::MovieScene::FInstanceHandle InstanceHandle;

		bool IsValid() const;

		friend bool operator==(const FReplayInfo& A, const FReplayInfo& B)
		{
			return A.Section == B.Section && A.InstanceHandle == B.InstanceHandle;
		}
		friend bool operator!=(const FReplayInfo& A, const FReplayInfo& B)
		{
			return A.Section != B.Section || A.InstanceHandle != B.InstanceHandle;
		}
	};

	TArray<FReplayInfo> CurrentReplayInfos;

	IConsoleVariable* ShowFlagMotionBlur = nullptr;

	static FDelegateHandle PreLoadMapHandle;
	static FDelegateHandle PostLoadMapHandle;
	static FDelegateHandle ReplayStartedHandle;
	static FTimerHandle ReEvaluateHandle;
};
