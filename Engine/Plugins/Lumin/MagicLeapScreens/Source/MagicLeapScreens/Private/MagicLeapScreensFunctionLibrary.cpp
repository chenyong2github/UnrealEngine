// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapScreensFunctionLibrary.h"
#include "MagicLeapScreensPlugin.h"

void UMagicLeapScreensFunctionLibrary::GetWatchHistoryAsync(const FMagicLeapScreensHistoryRequestResultDelegate& ResultDelegate)
{
#if WITH_MLSDK
	GetMagicLeapScreensPlugin().GetWatchHistoryEntriesAsync(TOptional<FMagicLeapScreensHistoryRequestResultDelegate>(ResultDelegate));
#else
	TArray<FMagicLeapScreensWatchHistoryEntry> ResultEntries;
	ResultDelegate.ExecuteIfBound(false, ResultEntries);
#endif // WITH_MLSDK
}

void UMagicLeapScreensFunctionLibrary::AddToWatchHistoryAsync(const FMagicLeapScreensWatchHistoryEntry& NewEntry, const FMagicLeapScreensEntryRequestResultDelegate& ResultDelegate)
{
#if WITH_MLSDK
	GetMagicLeapScreensPlugin().AddToWatchHistoryAsync(NewEntry, TOptional<FMagicLeapScreensEntryRequestResultDelegate>(ResultDelegate));
#else
	ResultDelegate.ExecuteIfBound(false, NewEntry);
#endif // WITH_MLSDK
}

void UMagicLeapScreensFunctionLibrary::UpdateWatchHistoryEntryAsync(const FMagicLeapScreensWatchHistoryEntry& UpdateEntry, const FMagicLeapScreensEntryRequestResultDelegate& ResultDelegate)
{
#if WITH_MLSDK
	GetMagicLeapScreensPlugin().UpdateWatchHistoryEntryAsync(UpdateEntry, TOptional<FMagicLeapScreensEntryRequestResultDelegate>(ResultDelegate));
#else
	ResultDelegate.ExecuteIfBound(false, UpdateEntry);
#endif // WITH_MLSDK
}

bool UMagicLeapScreensFunctionLibrary::RemoveWatchHistoryEntry(const FGuid& ID)
{
	return GetMagicLeapScreensPlugin().RemoveWatchHistoryEntry(ID);
}

bool UMagicLeapScreensFunctionLibrary::ClearWatchHistory()
{
	return GetMagicLeapScreensPlugin().ClearWatchHistory();
}

void UMagicLeapScreensFunctionLibrary::UpdateScreenTransformAsync(const FMagicLeapScreenTransform& UpdateTransform, const FMagicLeapScreenTransformRequestResultDelegate& ResultDelegate)
{
#if WITH_MLSDK
	GetMagicLeapScreensPlugin().UpdateScreenTransformAsync(UpdateTransform, ResultDelegate);
#else
	ResultDelegate.ExecuteIfBound(false);
#endif // WITH_MLSDK
}

bool UMagicLeapScreensFunctionLibrary::GetScreenTransform(FMagicLeapScreenTransform& ScreenTransform)
{
	return GetMagicLeapScreensPlugin().GetScreenTransform(ScreenTransform);
}

bool UMagicLeapScreensFunctionLibrary::GetScreensTransforms(TArray<FMagicLeapScreenTransform>& ScreensTransforms)
{
	return GetMagicLeapScreensPlugin().GetScreensTransforms(ScreensTransforms);
}
