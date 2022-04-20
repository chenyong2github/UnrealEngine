// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertHeaderRowUtils.generated.h"

class FMenuBuilder;
class SHeaderRow;

USTRUCT()
struct CONCERTSHAREDSLATE_API FColumnVisibilitySnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	FString Snapshot;
};

namespace UE::ConcertSharedSlate
{
	DECLARE_DELEGATE_OneParam(FSaveColumnVisibilitySnapshot, const FColumnVisibilitySnapshot& /*Snapshot*/);
	
	/**
	 * Creates a widget for hiding a SHeaderRow::FColumn when it is right-clicked.
	 */
	CONCERTSHAREDSLATE_API TSharedRef<SWidget> MakeHideColumnContextMenu(const TSharedRef<SHeaderRow>& HeaderRow, const FName ForColumnID);
	CONCERTSHAREDSLATE_API void AddHideColumnEntry(const TSharedRef<SHeaderRow>& HeaderRow, const FName ForColumnID, FMenuBuilder& MenuBuilder);

	/**
	 * Inspects the hidden rows on the header row and an entry for showing each hidden column.
	 * @param HeaderRow To retrieve column information
	 * @param MenuBuilder Where to add the menu entries
	 */
	CONCERTSHAREDSLATE_API void AddEntriesForShowingHiddenRows(const TSharedRef<SHeaderRow>& HeaderRow, FMenuBuilder& MenuBuilder);

	/** Exports the visibility state of each column so it can be restore later */
	CONCERTSHAREDSLATE_API FColumnVisibilitySnapshot SnapshotColumnVisibilityState(const TSharedRef<SHeaderRow>& HeaderRow);
	/** Restores the column visibilities from an exported state */
	CONCERTSHAREDSLATE_API void RestoreColumnVisibilityState(const TSharedRef<SHeaderRow>& HeaderRow, const FColumnVisibilitySnapshot& Snapshot);
};

