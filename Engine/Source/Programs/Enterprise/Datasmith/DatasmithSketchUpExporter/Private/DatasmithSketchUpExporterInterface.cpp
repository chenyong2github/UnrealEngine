// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithSketchUpExporterInterface.h"

// SketchUp to Datasmith exporter classes.
#include "DatasmithSketchUpDialogs.h"
#include "DatasmithSketchUpExporter.h"
#include "DatasmithSketchUpSummary.h"


FDatasmithSketchUpExporterInterface& FDatasmithSketchUpExporterInterface::GetSingleton()
{
	static FDatasmithSketchUpExporterInterface Singleton;

	return Singleton;
}

void FDatasmithSketchUpExporterInterface::ShowOptionsDialog(
	bool bInModelHasSelection
)
{
	FDatasmithSketchUpDialogs::ShowOptionsDialog(bInModelHasSelection);
}

bool FDatasmithSketchUpExporterInterface::ConvertFromSkp(
	const std::string&              InInputPath,
	const std::string&              InOutputPath,
	SketchUpPluginProgressCallback* InProgressCallback,
	void*                           /* InReserved */
)
{
	return FDatasmithSketchUpExporter().Convert(InInputPath, InOutputPath, InProgressCallback);
}

void FDatasmithSketchUpExporterInterface::ShowSummaryDialog()
{
	// Get the summary of the export process.
	FString const& Summary = FDatasmithSketchUpSummary::GetSingleton().GetSummary();

	// When the summary is not empty, display it in a modal dialog.
	if (!Summary.IsEmpty())
	{
		FDatasmithSketchUpDialogs::ShowSummaryDialog(Summary);
	}
}
