// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneSequencePlayer.h"
#include "TemplateSequencePlayer.generated.h"

class ATemplateSequenceActor;
class UTemplateSequence;

UCLASS(BlueprintType)
class TEMPLATESEQUENCE_API UTemplateSequencePlayer : public UMovieSceneSequencePlayer
{
public:

	GENERATED_BODY()

	UTemplateSequencePlayer(const FObjectInitializer&);

public:

	UFUNCTION(BlueprintCallable, Category = "Sequencer|Player", meta = (WorldContext = "WorldContextObject", DynamicOutputParam = "OutActor"))
	static UTemplateSequencePlayer* CreateTemplateSequencePlayer(UObject* WorldContextObject, UTemplateSequence* TemplateSequence, FMovieSceneSequencePlaybackSettings Settings, ATemplateSequenceActor*& OutActor);

public:

	void Initialize(UMovieSceneSequence* InSequence, UWorld* InWorld, const FMovieSceneSequencePlaybackSettings& InSettings);

	// IMovieScenePlayer interface
	virtual UObject* GetPlaybackContext() const override;

private:

	/** The world this player will spawn actors in, if needed. */
	TWeakObjectPtr<UWorld> World;
};
