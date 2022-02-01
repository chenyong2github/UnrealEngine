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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	int32 StartFrameOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	int32 EndFrameOffset;

	/**
	 * If true, the sequence will be inserted immediately on recording start and any time Reset()
	 * is called, paused at the first frame indefinitely until either triggered or stopped.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	bool bHoldAtFirstFrame;

	/** 0 is single playthrough, >= 1 is (n+1) playthroughs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	int32 NumLoops;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SequencerPlaylists", meta=(NoResetToDefault))
	float PlaybackSpeed = 1.0f;
};
