// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Datasmith SDK.
#include "Containers/UnrealString.h"


class FDatasmithSketchUpDialogs
{
public:

	// Displays a modal dialog which controls options supported by the exporter.
	static void ShowOptionsDialog(
		bool bInModelHasSelection // indicates if the SketchUp model has a current selection set
	);

	// Displays a modal dialog showing a summary of the last export process.
	static void ShowSummaryDialog(
		FString const& InExportSummary // summary of the last export process
	);
};
