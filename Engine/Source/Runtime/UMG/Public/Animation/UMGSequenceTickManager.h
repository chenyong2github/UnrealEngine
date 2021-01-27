// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "MovieSceneSequenceTickManager.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "UMGSequenceTickManager.generated.h"

class UMovieSceneEntitySystemLinker;
class UUserWidget;

/**
 * An automatically created global object that will manage all widget animations.
 */
UCLASS()
class UMG_API UUMGSequenceTickManager : public UObject
{
public:
	GENERATED_BODY()

	UUMGSequenceTickManager(const FObjectInitializer& Init);

	UMovieSceneEntitySystemLinker* GetLinker() { return Linker; }
	FMovieSceneEntitySystemRunner& GetRunner() { return Runner; }

	void AddLatentAction(FMovieSceneSequenceLatentActionDelegate Delegate);
	void ClearLatentActions(UObject* Object);
	void RunLatentActions();

	static UUMGSequenceTickManager* Get(UObject* PlaybackContext);

	void ForceFlush();

	void AddWidget(UUserWidget* InWidget);
	void RemoveWidget(UUserWidget* InWidget);

private:
	virtual void BeginDestroy() override;

	void HandleSlatePostTick(float DeltaSeconds);
	void TickWidgetAnimations(float DeltaSeconds);

private:

	UPROPERTY(transient)
	TSet<TWeakObjectPtr<UUserWidget>> WeakUserWidgets;

	UPROPERTY(transient)
	TObjectPtr<UMovieSceneEntitySystemLinker> Linker;

	FMovieSceneEntitySystemRunner Runner;

	bool bIsTicking;
	FDelegateHandle SlateApplicationPreTickHandle, SlateApplicationPostTickHandle;

	FMovieSceneLatentActionManager LatentActionManager;
};
