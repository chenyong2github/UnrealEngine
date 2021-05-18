// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "MovieSceneSequenceTickManager.generated.h"

class UMovieSceneEntitySystemLinker;

DECLARE_DELEGATE(FMovieSceneSequenceLatentActionDelegate);

/**
 * Utility class for running latent actions created from sequence players.
 */
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
 * Interface for sequence actors that are to be ticked by the tick manager.
 */
UINTERFACE()
class MOVIESCENE_API UMovieSceneSequenceActor
	: public UInterface
{
public:
	GENERATED_BODY()
};

class MOVIESCENE_API IMovieSceneSequenceActor
{
public:
	GENERATED_BODY()

	virtual void TickFromSequenceTickManager(float DeltaSeconds) = 0;
};

/**
 * A structure for storing pointers to a sequence actor.
 */
USTRUCT()
struct FMovieSceneSequenceActorPointers
{
	GENERATED_BODY()

	UPROPERTY()
	AActor* SequenceActor = nullptr;

	UPROPERTY()
	TScriptInterface<IMovieSceneSequenceActor> SequenceActorInterface;
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

	void RegisterSequenceActor(AActor* InActor);
	void UnregisterSequenceActor(AActor* InActor);

	void AddLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate);
	void RunLatentActions();

	void ClearLatentActions(UObject* Object);

	static UMovieSceneSequenceTickManager* Get(UObject* PlaybackContext);

private:
	virtual void BeginDestroy() override;

	void TickSequenceActors(float DeltaSeconds);

private:

	UPROPERTY(transient)
	TArray<FMovieSceneSequenceActorPointers> SequenceActors;

	UPROPERTY(transient)
	UMovieSceneEntitySystemLinker* Linker;

	FMovieSceneEntitySystemRunner Runner;

	FDelegateHandle WorldTickDelegateHandle;

	FMovieSceneLatentActionManager LatentActionManager;
};
