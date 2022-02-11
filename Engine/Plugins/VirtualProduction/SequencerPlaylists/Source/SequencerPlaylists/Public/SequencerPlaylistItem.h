// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SequencerPlaylistItem.generated.h"


class ISequencer;
class UMovieSceneSection;


UCLASS(BlueprintType, Abstract, Within=SequencerPlaylist)
class USequencerPlaylistItem : public UObject
{
	GENERATED_BODY()

public:
	virtual FText GetDisplayName() PURE_VIRTUAL(USequencerPlaylistItem::GetDisplayName, return FText::GetEmpty(); )

public:
	/** Number of frames by which to clip the in point of sections played from this item. Will also affect the first frame for hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	int32 StartFrameOffset;

	/** Number of frames by which to clip the out point of sections played from this item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	int32 EndFrameOffset;

	/**
	 * If true, the sequence will be inserted immediately on recording start and any time Reset()
	 * is called, paused at the first frame indefinitely until subsequently played or stopped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	bool bHoldAtFirstFrame;

	/** 0 is single playthrough, >= 1 is (n+1) playthroughs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	int32 NumLoops;

	/** Speed multiplier at which to play sections created by this item. A value of 1 will result in playback at the original speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	float PlaybackSpeed = 1.0f;

	/** Disables playback of this item and prevents it from entering a hold state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	bool bMute;
};
