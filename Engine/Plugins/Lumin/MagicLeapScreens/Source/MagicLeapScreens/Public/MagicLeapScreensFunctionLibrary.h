// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapScreensTypes.h"
#include "MagicLeapScreensFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPSCREENS_API UMagicLeapScreensFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
		Asynchronously requests all watch history entries that belong to this application.

		@param[in] ResultDelegate Delegate that is called once the request has been resolved.
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static void GetWatchHistoryAsync(const FMagicLeapScreensHistoryRequestResultDelegate& ResultDelegate);

	/**
		Asynchronously requests to adds a new entry into the watch history.

		@param[in] NewEntry Watch history entry to add.
		@param[in] ResultDelegate Delegate that is called once the add request has been resolved. The resulting FMagicLeapScreensWatchHistoryEntry returned.
		on success contains the new ID of the added entry.
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static void AddToWatchHistoryAsync(const FMagicLeapScreensWatchHistoryEntry& NewEntry, const FMagicLeapScreensEntryRequestResultDelegate& ResultDelegate);

	/**
		Asynchronously requests to update an entry in the watch history. Only entries associated with this application can be updated.

		@param[in] UpdateEntry Watch history entry to update. The ID of this entry must be valid and in the watch history in order for the update to resolve successfully
		@param[in] ResultDelegate Delegate that is called once the update request has been resolved.
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static void UpdateWatchHistoryEntryAsync(const FMagicLeapScreensWatchHistoryEntry& UpdateEntry, const FMagicLeapScreensEntryRequestResultDelegate& ResultDelegate);

	/**
		Asynchronously requests to update a screen transform

		@param[in] UpdateTransform Screen transform to update. The ID of this entry must be valid in order for the update to resolve successfully
		@param[in] ResultDelegate Delegate that is called once the update request has been resolved.
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static void UpdateScreenTransformAsync(const FMagicLeapScreenTransform& UpdateTransform, const FMagicLeapScreenTransformRequestResultDelegate& ResultDelegate);

	/**
		Removes an entry from the watch history that corresponds with the ID passed. Only entries associated with this application can be removed.

		@param[in] ID ID of the watch history entry to remove.
		@return True when the entry is sucessfully removed.
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static bool RemoveWatchHistoryEntry(const FGuid& ID);

	/**
		Removes all watch history entries.

		@return True when all watch history entries have been successfully removed.
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static bool ClearWatchHistory();

	/**
		Gets the transform for all watch history entries.

		@param[out] ScreenTransforms Array of transforms that corresponds to all current watch history entries.
		@return True when the request for all transforms succeeds 
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static bool GetScreensTransforms(TArray<FMagicLeapScreenTransform>& ScreenTransforms);

	/**
		Gets a single screen transform.

		@param[out] ScreenTransform Transform that corresponds to a watch history entry with the ID passed via command line as 'screenId'.
		@return True when the request for the transform succeeds
	*/
	UFUNCTION(BlueprintCallable, Category = "Screens Function Library|MagicLeap")
	static bool GetScreenTransform(FMagicLeapScreenTransform& ScreenTransform);

	/**
		Delegate used to relay the results of GetWatchHistory.
	*/
	UPROPERTY()
	FMagicLeapScreensHistoryRequestResultDelegate HistoryResultDelegate;

	/**
		Delegate used to relay the results of both AddToWatchHistory and UpdateWatchHistoryEntry.
	*/
	UPROPERTY()
	FMagicLeapScreensEntryRequestResultDelegate EntryResultDelegate;

	/**
		Delegate used to relay the results of UpdateScreenTransformAsync.
	*/
	UPROPERTY()
	FMagicLeapScreenTransformRequestResultDelegate ScreenTransformResultDelegate;
};
