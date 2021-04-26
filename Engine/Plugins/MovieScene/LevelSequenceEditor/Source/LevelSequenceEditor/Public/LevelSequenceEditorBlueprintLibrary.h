// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/QualifiedFrameTime.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneObjectBindingID.h"
#include "LevelSequenceEditorBlueprintLibrary.generated.h"

class ISequencer;
class ULevelSequence;
class UMovieSceneFolder;
class UMovieSceneSection;
class UMovieSceneTrack;

USTRUCT(BlueprintType)
struct FSequencerChannelProxy
{
	GENERATED_BODY()

	FSequencerChannelProxy()
		: Section(nullptr)
	{}

	FSequencerChannelProxy(const FName& InChannelName, UMovieSceneSection* InSection)
		: ChannelName(InChannelName)
		, Section(InSection)
	{}

	UPROPERTY(BlueprintReadWrite, Category=Channel)
	FName ChannelName;

	UPROPERTY(BlueprintReadWrite, Category=Channel)
	UMovieSceneSection* Section;
};


UCLASS()
class LEVELSEQUENCEEDITOR_API ULevelSequenceEditorBlueprintLibrary : public UBlueprintFunctionLibrary
{
public:

	GENERATED_BODY()

	/*
	 * Open a level sequence asset
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static bool OpenLevelSequence(ULevelSequence* LevelSequence);

	/*
	 * Get the currently opened root/master level sequence asset
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static ULevelSequence* GetCurrentLevelSequence();

	/*
	 * Get the currently focused/viewed level sequence asset if there is a hierarchy of sequences.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static ULevelSequence* GetFocusedLevelSequence();

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
	 * Set global playback position for the current level sequence in frames
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SetCurrentTime(int32 NewFrame);

	/**
	 * Get the current global playback position in frames
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static int32 GetCurrentTime();

	/**
	 * Set local playback position for the current level sequence in frames
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SetCurrentLocalTime(int32 NewFrame);

	/**
	 * Get the current local playback position in frames
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static int32 GetCurrentLocalTime();

	/**
	 * Play from the current time to the requested time in frames
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void PlayTo(FMovieSceneSequencePlaybackParams PlaybackParams);

public:

	/** Check whether the sequence is actively playing. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static bool IsPlaying();

public:

	/** Gets the currently selected tracks. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static TArray<UMovieSceneTrack*> GetSelectedTracks();

	/** Gets the currently selected sections. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static TArray<UMovieSceneSection*> GetSelectedSections();

	/** Gets the currently selected channels. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static TArray<FSequencerChannelProxy> GetSelectedChannels();

	/** Gets the currently selected folders. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static TArray<UMovieSceneFolder*> GetSelectedFolders();

	/** Gets the currently selected Object Guids*/
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static TArray<FGuid> GetSelectedObjects();

	/** Select tracks */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SelectTracks(const TArray<UMovieSceneTrack*>& Tracks);

	/** Select sections */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SelectSections(const TArray<UMovieSceneSection*>& Sections);

	/** Select channels */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SelectChannels(const TArray<FSequencerChannelProxy>& Channels);

	/** Select folders */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SelectFolders(const TArray<UMovieSceneFolder*>& Folders);

	/** Select objects by GUID */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SelectObjects(TArray<FGuid> ObjectBinding);

	/** Empties the current selection. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void EmptySelection();

public:

	/** Refresh Sequencer UI. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void RefreshCurrentLevelSequence();

	/** Get the object bound to the given binding ID with the current level sequence editor */
	UFUNCTION(BlueprintPure, Category="Level Sequence Editor")
	static TArray<UObject*> GetBoundObjects(FMovieSceneObjectBindingID ObjectBinding);

	/** Check whether the current level sequence and its descendants are locked for editing. */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	static bool IsLevelSequenceLocked();

	/** Sets the lock for the current level sequence and its descendants for editing. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	static void SetLockLevelSequence(bool bLock);

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