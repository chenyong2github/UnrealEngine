// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityInstantiatorSystem.h"
#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "EntitySystem/MovieSceneOverlappingEntityTracker.h"
#include "Engine/EngineTypes.h"
#include "MovieSceneComponentMobilitySystem.generated.h"

UCLASS(MinimalAPI)
class UMovieSceneComponentMobilitySystem
	: public UMovieSceneEntityInstantiatorSystem
	, public IMovieScenePreAnimatedStateSystemInterface
{
public:

	GENERATED_BODY()

	UMovieSceneComponentMobilitySystem(const FObjectInitializer& ObjInit);

	void AddPendingRestore(USceneComponent* SceneComponent, EComponentMobility::Type InMobility);

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:

	virtual bool IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const override;
	virtual void OnLink() override final;
	virtual void OnUnlink() override final;
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override final;

	//~ IMovieScenePreAnimatedStateSystemInterface interface
	virtual void SaveGlobalPreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;
	virtual void RestorePreAnimatedState(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	void TagGarbage(UMovieSceneEntitySystemLinker*);

private:

	UE::MovieScene::TOverlappingEntityTracker_BoundObject<EComponentMobility::Type> MobilityTracker;

	UE::MovieScene::FEntityComponentFilter Filter;

	TArray<TTuple<USceneComponent*, EComponentMobility::Type>> PendingMobilitiesToRestore;
};

