// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneOverlappingEntityTracker.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"

#include "MovieSceneMaterialParameterSystem.generated.h"

class UMovieScenePiecewiseDoubleBlenderSystem;

namespace UE::MovieScene
{

struct FAnimatedMaterialParameterInfo
{
	int32 NumContributors = 0;
	FMovieSceneEntityID OutputEntityID;
	FMovieSceneBlendChannelID BlendChannelID;
};

} // namespace UE::MovieScene


/**
 * System responsible for tracking and animating material parameter entities.
 * Operates on the following component types from FMovieSceneTracksComponentTypes:
 *
 * Instantiation: Tracks any BoundMaterial with a ScalarParameterName, ColorParameterName or VectorParameterName.
 *                Manages adding BlendChannelInputs and Outputs where multiple entities animate the same parameter
 *                on the same bound material.
 *                BoundMaterials may be a UMaterialInstanceDynamic, or a UMaterialParameterCollectionInstance.
 *
 * Evaluation:    Visits any BoundMaterial with the supported parameter names and either a BlendChannelOutput component
 *                or no BlendChannelInput, and applies the resulting parameter to the bound material instance.
 */
UCLASS(MinimalAPI)
class UMovieSceneMaterialParameterSystem
	: public UMovieSceneEntitySystem
{
public:

	GENERATED_BODY()

	UMovieSceneMaterialParameterSystem(const FObjectInitializer& ObjInit);

private:

	virtual void OnLink() override;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	void OnInstantiation();
	void OnEvaluation(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents);

private:

	/** Overlapping trackers that track multiple entities animating the same bound object and name */
	UE::MovieScene::TOverlappingEntityTracker<UE::MovieScene::FAnimatedMaterialParameterInfo, UObject*, FName> ScalarParameterTracker;
	UE::MovieScene::TOverlappingEntityTracker<UE::MovieScene::FAnimatedMaterialParameterInfo, UObject*, FName> VectorParameterTracker;

public:

	UPROPERTY()
	TObjectPtr<UMovieScenePiecewiseDoubleBlenderSystem> DoubleBlenderSystem;
};
