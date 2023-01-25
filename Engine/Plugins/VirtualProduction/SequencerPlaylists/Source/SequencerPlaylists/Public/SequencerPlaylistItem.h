// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SequencerPlaylistItem.generated.h"


UCLASS(BlueprintType, Abstract, Within=SequencerPlaylist)
class USequencerPlaylistItem : public UObject
{
	GENERATED_BODY()

public:
	virtual FText GetDisplayName() PURE_VIRTUAL(USequencerPlaylistItem::GetDisplayName, return FText::GetEmpty(); )

public:
	/** Number of frames by which to clip the in point of sections played from this item. Will also affect the first frame for hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequencer Playlists")
	int32 StartFrameOffset;

	/** Number of frames by which to clip the out point of sections played from this item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequencer Playlists")
	int32 EndFrameOffset;

#if WITH_EDITORONLY_DATA
	/**
	 * If true, the sequence will be inserted immediately on recording start and any time Reset()
	 * is called, paused at the first frame indefinitely until subsequently played or stopped.
	 */
	UE_DEPRECATED(5.2, "Holds are now created by invoking Pause prior to beginning recording.")
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Holds are now created by invoking Pause prior to beginning recording."))
	bool bHoldAtFirstFrame_DEPRECATED;
#endif

	/** 0 is single playthrough, >= 1 is (n+1) playthroughs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequencer Playlists")
	int32 NumLoops;

	/** Speed multiplier at which to play sections created by this item. A value of 1 will result in playback at the original speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequencer Playlists")
	float PlaybackSpeed = 1.0f;

	/** Disable playback of this item and prevent it from entering a hold state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sequencer Playlists")
	bool bMute;
};
