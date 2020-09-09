// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "MagicLeapMusicServiceTypes.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapMusicServiceProviderError : uint8
{
	/** Music service has not reported any errors */
	None,
	/** Music service has reported a connectivity error */
	Connectivity,
	/** Music service has reported a timeout */
	Timeout,
	/** Music service has reported a general playback error */
	GeneralPlayback,
	/** Music service has reported an app privilege error */
	Privilege,
	/** Music service has reported an error specific to it's implementation */
	ServiceSpecific,
	/** Music service has reported it has no available memory */
	NoMemory,
	/** Music service has reported an unspecified error */
	Unspecified
};

UENUM(BlueprintType)
enum class EMagicLeapMusicServicePlaybackState : uint8
{
	/** Music service is currently playing */
	Playing,
	/** Music service is currently paused */
	Paused,
	/** Music service is currently stopped */
	Stopped,
	/** Music service has errored */
	Error,
	/** Playback state of the music service is not known */
	Unknown
};

UENUM(BlueprintType)
enum class EMagicLeapMusicServicePlaybackShuffleState : uint8
{
	/** Enable shuffling */
	On,
	/** Disable shuffling */
	Off,
	/** Shuffle state of music service is not known */
	Unknown
};

UENUM(BlueprintType)
enum class EMagicLeapMusicServicePlaybackRepeatState : uint8
{
	/** Disable playback repetition */
	Off,
	/** Enable single track playback repetition */
	Song,
	/** Enable playlist playback repetition */
	Album,
	/** Repeat state of music service is not known */
	Unkown
};

UENUM(BlueprintType)
enum class EMagicLeapMusicServiceStatus : uint8
{
	/** The music service provider's context has changed */
	ContextChanged,
	/** The music service provider has successfully been created */
	Created,
	/** Client has successfully logged into the connected music service provider */
	LoggedIn,
	/** Client has successfully logged out of the connected music service provider */
	LoggedOut,
	/** Music service provider has advanced the current track */
	NextTrack,
	/** Music service provider has rewinded the current track */
	PreviousTrack,
	/** Music service provider has changed the track */
	TrackChanged,
	/** The current status of the music service provider is not known */
	Unknown
};

USTRUCT(BlueprintType)
struct MAGICLEAPMUSICSERVICE_API FMagicLeapMusicServiceTrackMetadata
{
	GENERATED_BODY()

public:
	/** Title of the currently selected track */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Service|MagicLeap")
	FString TrackTitle;

	/** Name of the album of the currently selected track */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Service|MagicLeap")
	FString AlbumName;

	/** URL to the album of the currently selected track */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Service|MagicLeap")
	FString AlbumURL;

	/** URL to the album cover of the currently selected track */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Service|MagicLeap")
	FString AlbumCoverURL;

	/** Artist name of the currently selected track */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Service|MagicLeap")
	FString ArtistName;

	/** Artist URL of the currently selected track */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Service|MagicLeap")
	FString ArtistURL;

	/** Runtime length of the currently selected track */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Music Service|MagicLeap")
	FTimespan TrackLength;
};


/** Delegates used for music service callbacks. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapMusicServicePlaybackStateDelegate, const EMagicLeapMusicServicePlaybackState, PlaybackState);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapMusicServiceRepeatStateDelegate, const EMagicLeapMusicServicePlaybackRepeatState, RepeatState);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapMusicServiceShuffleStateDelegate, const EMagicLeapMusicServicePlaybackShuffleState, ShuffleState);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapMusicServiceMetadataDelegate, const FMagicLeapMusicServiceTrackMetadata&, Metadata);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapMusicServicePositionDelegate, const FTimespan, Position);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapMusicServiceErrorDelegate, const EMagicLeapMusicServiceProviderError, Error, const int32, ErrorCode);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapMusicServiceStatusDelegate, const EMagicLeapMusicServiceStatus, Status);
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapMusicServiceVolumeDelegate, const float, Volume);


USTRUCT(BlueprintType)
struct MAGICLEAPMUSICSERVICE_API FMagicLeapMusicServiceCallbacks
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServicePlaybackStateDelegate PlaybackStateDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServiceRepeatStateDelegate RepeatStateDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServiceShuffleStateDelegate ShuffleStateDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServiceMetadataDelegate MetadataDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServicePositionDelegate PositionDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServiceErrorDelegate ErrorDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServiceStatusDelegate StatusDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music Service|MagicLeap")
	FMagicLeapMusicServiceVolumeDelegate VolumeDelegate;
};
