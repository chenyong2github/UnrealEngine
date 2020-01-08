// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "MagicLeapScreensTypes.generated.h"


/** Channel watch history, may be displayed in the Screens Launcher application. */
USTRUCT(BlueprintType)
struct FMagicLeapScreensWatchHistoryEntry
{
	GENERATED_BODY()

public:
	/** Entry Identifier. Must be used to update and delete a given entry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Screens|MagicLeap")
	FGuid ID;

	/** Title of the media for which this entry is created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FString Title;

	/** Subtitle of the media for which this entry is created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FString Subtitle;

	/** Current media playback position. Can be fed from UMediaPlayer::GetTime(). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FTimespan PlaybackPosition;

	/** Total duration of the media. Can be fed from UMediaPlayer::GetDuration() */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FTimespan PlaybackDuration;

	/** Any data the application might want to save off in the watch history and then receive back from the Screens Launher. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FString CustomData;

	/** Thumbnail to be shown in the Screens Launcher application for this watch history entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	UTexture2D* Thumbnail;
};

/**
  Information required to place a screen in the world.

  This will be received from the Screens Launcher api, based on the previous screens spawned by user.
 */
USTRUCT(BlueprintType)
struct FMagicLeapScreenTransform
{
	GENERATED_BODY()

public:
	/** Entry identifier. Must be used to update a given entry. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Screens|MagicLeap")
	FGuid ID;

	/** Position of the screen in Unreal's world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FVector ScreenPosition;

	/** Orientation of the screen in Unreal's world space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FRotator ScreenOrientation;

	/** Dimensions of the screen in Unreal Units. The dimensions are axis-aligned with the orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Screens|MagicLeap")
	FVector ScreenDimensions;

	/** Screen scale, used to scale the screens dimensions */
	FVector ScreenScale3D;

	/** Version number */
	int32 VersionNumber;
};

/**
	Delegate used to relay the result of a Screens operation that involves a single watch history entry.
	For example updating or adding a history entry.

	@param[out] bSuccess True when the request is successful
	@param[out] WatchHistoryEntry Resulting watch history entry for which the operation was performed on.
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapScreensEntryRequestResultDelegate, const bool, bSuccess, const FMagicLeapScreensWatchHistoryEntry&, WatchHistoryEntry);

/**
	Delegate used to relay the result of getting the entire watch history.

	@param[out] bSuccess True when the request is successful.
	@param[out] WatchHistoryEntry Resulting array of watch histories returned by the operation.
*/
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapScreensHistoryRequestResultDelegate, const bool, bSuccess, const TArray<FMagicLeapScreensWatchHistoryEntry>&, WatchHistoryEntries);

/**
	Delegate used to relay the result of updating a screen's transform.

	@param[out] bSuccess True when the request is successful.
*/
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapScreenTransformRequestResultDelegate, const bool, bSuccess);

