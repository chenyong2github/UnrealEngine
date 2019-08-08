// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/QualifiedFrameTime.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSequenceEditorBlueprintLibrary.generated.h"

class ISequencer;
class ULevelSequence;

UCLASS()
class ULevelSequenceEditorBlueprintLibrary : public UBlueprintFunctionLibrary
{
public:

	GENERATED_BODY()

	/*
	 * Open a level sequence asset
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static bool OpenLevelSequence(ULevelSequence* LevelSequence);

	/*
	 * Get the currently opened level sequence asset
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static ULevelSequence* GetCurrentLevelSequence();

	/*
	 * Close
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void CloseLevelSequence();

	/**
	 * Play the current level sequence
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void Play();

	/**
	 * Pause the current level sequence
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void Pause();

public:

	/**
	 * Set playback position for the current level sequence in frames
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SetCurrentTime(int32 NewFrame);

	/**
	 * Get the current playback position in frames
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static int32 GetCurrentTime();

public:

	/** Check whether the sequence is actively playing. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static bool IsPlaying();

	/** Check whether the sequence is paused. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static bool IsPaused();

public:

	/*
	 * Callbacks
	 */

public:

	 /**
	  * Internal function to assign a sequencer singleton.
	  * NOTE: Only to be called by LevelSequenceEditor::Construct.
	  */
	static void SetSequencer(TSharedRef<ISequencer> InSequencer);
};