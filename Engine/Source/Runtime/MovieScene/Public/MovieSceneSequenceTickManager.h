// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "MovieSceneSequenceTickManager.generated.h"

class UMovieSceneEntitySystemLinker;

DECLARE_DELEGATE(FMovieSceneSequenceLatentActionDelegate);

class MOVIESCENE_API FMovieSceneLatentActionManager
{
public:
	void AddLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate);
	void ClearLatentActions(UObject* Object);
	void ClearLatentActions();

	void RunLatentActions(FMovieSceneEntitySystemRunner& Runner);

	bool IsEmpty() const { return LatentActions.Num() == 0; }

private:
	TArray<FMovieSceneSequenceLatentActionDelegate> LatentActions;

	bool bIsRunningLatentActions = false;
};

/**
 * An automatically created global object that will manage all level sequence actors' updates.
 */
UCLASS()
class MOVIESCENE_API UMovieSceneSequenceTickManager : public UObject
{
public:
	GENERATED_BODY()

	UMovieSceneSequenceTickManager(const FObjectInitializer& Init);

	UMovieSceneEntitySystemLinker* GetLinker() { return Linker; }
	FMovieSceneEntitySystemRunner& GetRunner() { return Runner; }

	void AddLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate);
	void RunLatentActions();

	void ClearLatentActions(UObject* Object);

	static UMovieSceneSequenceTickManager* Get(UObject* PlaybackContext);

private:
	virtual void BeginDestroy() override;

	void TickSequenceActors(float DeltaSeconds);

public:
	UPROPERTY(transient)
	TArray<TObjectPtr<AActor>> SequenceActors;

private:
	UPROPERTY(transient)
	TObjectPtr<UMovieSceneEntitySystemLinker> Linker;

	FMovieSceneEntitySystemRunner Runner;

	FDelegateHandle WorldTickDelegateHandle;

	FMovieSceneLatentActionManager LatentActionManager;
};
