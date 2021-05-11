// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"

#include "MovieScenePreAnimatedStateSystem.generated.h"

namespace UE
{
namespace MovieScene
{

/** Structure that manages the lifetime of the pre-animated state extension while entities exist with the restore state tag */
struct FPreAnimatedStateExtensionReference
{
	FPreAnimatedStateExtensionReference() = default;
	FPreAnimatedStateExtensionReference(UMovieSceneEntitySystemLinker* Linker)
	{
		Update(Linker);
	}

	TSharedPtr<FPreAnimatedStateExtension> Get() const;

	TSharedPtr<FPreAnimatedStateExtension> Update(UMovieSceneEntitySystemLinker* Linker);

private:
	/** Weak ref to the extension - this is always used for access, and will remain valid as long as there are any global state captures, or RestoreState entities */
	TWeakPtr<FPreAnimatedStateExtension>   WeakPreAnimatedStateExtension;
	/** Strong ref to the extension that keeps the extension alive if there are RestoreState entities in the entity manager */
	TSharedPtr<FPreAnimatedStateExtension> PreAnimatedStateExtensionRef;
};

} // namespace MovieScene
} // namespace UE

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

	struct FPreAnimationParameters
	{
		UE::MovieScene::FSystemTaskPrerequisites* Prerequisites;
		UE::MovieScene::FSystemSubsequentTasks* Subsequents;
		UE::MovieScene::FPreAnimatedStateExtension* CacheExtension;
	};

	virtual void SavePreAnimatedState(const FPreAnimationParameters& InParameters) {}
	virtual void RestorePreAnimatedState(const FPreAnimationParameters& InParameters) {}

private:

	UE_DEPRECATED(4.26, "Please override the method that takes a FPreAnimationParameters")
	virtual void SavePreAnimatedState(UE::MovieScene::FSystemTaskPrerequisites& InPrerequisites, UE::MovieScene::FSystemSubsequentTasks& Subsequents) final {}

	UE_DEPRECATED(4.26, "Please override the method that takes a FPreAnimationParameters")
	virtual void SaveGlobalPreAnimatedState(UE::MovieScene::FSystemTaskPrerequisites& InPrerequisites, UE::MovieScene::FSystemSubsequentTasks& Subsequents) final {}

	UE_DEPRECATED(4.26, "Please override the method that takes a FPreAnimationParameters")
	virtual void RestorePreAnimatedState(UE::MovieScene::FSystemTaskPrerequisites& InPrerequisites, UE::MovieScene::FSystemSubsequentTasks& Subsequents) final {}
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
	virtual void OnUnlink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	UE::MovieScene::FPreAnimatedStateExtensionReference PreAnimatedStateRef;
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

private:

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;
	virtual void OnLink() override;
	virtual void OnUnlink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	TSharedPtr<UE::MovieScene::FPreAnimatedStateExtension> PreAnimatedStateRef;
};
