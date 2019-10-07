// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// Standard library.
#include "string"

class SketchUpPluginProgressCallback;


// SketchUp application information passed to the Datasmith scene exporter.
#define SKETCHUP_HOST_NAME       TEXT("SketchUp")
#define SKETCHUP_VENDOR_NAME     TEXT("Trimble Inc.")
#define SKETCHUP_PRODUCT_NAME    TEXT("SketchUp Pro")
#define SKETCHUP_PRODUCT_VERSION TEXT("Version Unknown")


class FDatasmithSketchUpExporter
{
public:

	// Performs the conversion from a temporary SketchUp input file to a Datasmith output file
	// using options set during the ShowOptionsDialog method,
	// and returns true on success or false on failure or cancellation.
	bool Convert(
		std::string const&              InInputPath,       // temporary SketchUp input file path in UTF-8
		std::string const&              InOutputPath,      // Datasmith output file path in UTF-8
		SketchUpPluginProgressCallback* InProgressCallback // SketchUp progress interface callback (can be null)
	);

private:

	// Give export feedback on the SketchUp progress dialog.
	void SetProgress(
		SketchUpPluginProgressCallback* InProgressCallback, // SketchUp progress interface callback (can be null)
		double                          InPercentDone,      // progress percent done from 0 to 100
		std::string const&              InMessage           // progress message in UTF-8
	);
};
