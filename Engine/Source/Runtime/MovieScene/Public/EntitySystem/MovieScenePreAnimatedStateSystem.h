// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"

#include "MovieScenePreAnimatedStateSystem.generated.h"


UINTERFACE()
class UMovieScenePreAnimatedStateSystemInterface : public UInterface
{
	GENERATED_BODY()
};


/**
 * Interface that can be added to any entity system in the 'instantiation' phase to implement save / restore state
 * with its system dependencies strictly saved in order, and restored in reverse order
 */
class IMovieScenePreAnimatedStateSystemInterface
{
public:
	GENERATED_BODY()

	virtual void SavePreAnimatedState(UE::MovieScene::FSystemTaskPrerequisites& InPrerequisites, UE::MovieScene::FSystemSubsequentTasks& Subsequents) {}
	virtual void SaveGlobalPreAnimatedState(UE::MovieScene::FSystemTaskPrerequisites& InPrerequisites, UE::MovieScene::FSystemSubsequentTasks& Subsequents) {}

	virtual void RestorePreAnimatedState(UE::MovieScene::FSystemTaskPrerequisites& InPrerequisites, UE::MovieScene::FSystemSubsequentTasks& Subsequents) {}

	virtual void DiscardPreAnimatedStateForObject(UObject& Object) {}
};



/**
 * System that becomes relevant if there are any entites tagged RestoreState,
 * or UMovieSceneEntitySystemLinker::ShouldCaptureGlobalState is true.
 * When run this system will iterate the instantiation phase in order, and call 
 * IMovieScenePreAnimatedStateSystemInterface::Save(Global)PreAnimatedState on any
 * systems that implement the necessary interface
 */
UCLASS(MinimalAPI)
class UMovieSceneCachePreAnimatedStateSystem
	: public UMovieSceneEntityInstantiatorSystem
{
public:

	GENERATED_BODY()

	UMovieSceneCachePreAnimatedStateSystem(const FObjectInitializer& ObjInit);

private:

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;
	virtual void OnLink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;
};



/**
 * System that becomes relevant if there are any entites tagged RestoreState,
 * or UMovieSceneEntitySystemLinker::ShouldCaptureGlobalState is true.
 * When run this system will iterate the instantiation phase in reverse order, and call 
 * IMovieScenePreAnimatedStateSystemInterface::Restore(Global)PreAnimatedState on any
 * systems that implement the necessary interface.
 */
UCLASS(MinimalAPI)
class UMovieSceneRestorePreAnimatedStateSystem
	: public UMovieSceneEntityInstantiatorSystem
{
public:

	GENERATED_BODY()

	UMovieSceneRestorePreAnimatedStateSystem(const FObjectInitializer& ObjInit);

	MOVIESCENE_API void DiscardPreAnimatedStateForObject(UObject& Object);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;
};
