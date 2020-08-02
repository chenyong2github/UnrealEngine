// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "Sections/MovieSceneLevelVisibilitySection.h"
#include "Engine/LevelStreaming.h"
#include "MovieSceneLevelVisibilitySystem.generated.h"

class UWorld;

namespace UE
{
namespace MovieScene
{
	struct FMovieSceneLevelStreamingSharedData
	{
		bool HasAnythingToDo() const;
		void AssignLevelVisibilityOverrides(TArrayView<const FName> LevelNames, ELevelVisibility Visibility, int32 Bias, FMovieSceneEntityID EntityID);
		void UnassignLevelVisibilityOverrides(TArrayView<const FName> LevelNames, ELevelVisibility Visibility, int32 Bias, FMovieSceneEntityID EntityID);
		void ApplyLevelVisibility(IMovieScenePlayer& Player);

	private:
		ULevelStreaming* GetLevel(FName SafeLevelName, UWorld& World);

	private:
		struct FVisibilityData
		{
			TOptional<bool> bPreviousState;

			void Add(FMovieSceneEntityID EntityID, int32 Bias, ELevelVisibility Visibility);
			void Remove(FMovieSceneEntityID EntityID);

			/** Check whether this visibility data is empty */
			bool IsEmpty() const;

			/** Returns whether or not this level name should be visible or not */
			TOptional<ELevelVisibility> CalculateVisibility() const;

		private:
			struct FVisibilityRequest
			{
				/** The entity that made the request */
				FMovieSceneEntityID EntityID;
				/** The bias of the entity */
				int32 Bias;
				/** The actual visibility requested */
				ELevelVisibility Visibility;
			};
			TArray<FVisibilityRequest, TInlineAllocator<2>> Requests;
		};
		TMap<FName, FVisibilityData> VisibilityMap;

		TMap<FName, TWeakObjectPtr<ULevelStreaming>> NameToLevelMap;
	};
}
}

UCLASS(MinimalAPI)
class UMovieSceneLevelVisibilitySystem : public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UMovieSceneLevelVisibilitySystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

private:

	/** Cached filter that tells us whether we need to run this frame */
	UE::MovieScene::FCachedEntityFilterResult_Match ApplicableFilter;
	UE::MovieScene::FMovieSceneLevelStreamingSharedData SharedData;
};
