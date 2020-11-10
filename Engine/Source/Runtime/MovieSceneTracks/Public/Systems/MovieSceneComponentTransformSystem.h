// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Systems/MovieScenePropertySystem.h"
#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "EntitySystem/MovieSceneOverlappingEntityTracker.h"
#include "MovieSceneTracksComponentTypes.h"

#include "MovieSceneComponentTransformSystem.generated.h"



UCLASS(MinimalAPI)
class UMovieScenePreAnimatedComponentTransformSystem
	: public UMovieSceneEntityInstantiatorSystem
	, public IMovieScenePreAnimatedStateSystemInterface
{
public:

	GENERATED_BODY()

	UMovieScenePreAnimatedComponentTransformSystem(const FObjectInitializer& ObjInit);

	void AddPendingRestoreTransform(UObject* Object, const UE::MovieScene::FIntermediate3DTransform& InTransform);

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;
	virtual void OnLink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	//~ IMovieScenePreAnimatedStateSystemInterface interface
	virtual void RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual void SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual void DiscardPreAnimatedStateForObject(UObject& Object) override;

	void TagGarbage(UMovieSceneEntitySystemLinker*);

private:

	UE::MovieScene::TOverlappingEntityTracker_BoundObject<UE::MovieScene::FIntermediate3DTransform> TrackedTransforms;

	TArray<TTuple<UObject*, UE::MovieScene::FIntermediate3DTransform>> TransformsToRestore;
};



UCLASS(MinimalAPI)
class UMovieSceneComponentTransformSystem : public UMovieScenePropertySystem
{
public:

	GENERATED_BODY()

	UMovieSceneComponentTransformSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
};
