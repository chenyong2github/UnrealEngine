// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// SketchUp SDK.
#include "DatasmithSketchUpSDKBegins.h"
#include "SketchUpAPI/import_export/modelexporterplugin.h"
#include "DatasmithSketchUpSDKCeases.h"


class FDatasmithSketchUpExporterInterface:
	public SketchUpModelExporterInterface
{
public:

	static FDatasmithSketchUpExporterInterface& GetSingleton();

	FDatasmithSketchUpExporterInterface() {}
	virtual ~FDatasmithSketchUpExporterInterface() {}

	// Implement interface SketchUpModelExporterInterface.
	// http://extensions.sketchup.com/developer_center/sketchup_c_api/sketchup/class_sketch_up_model_exporter_interface.html

	// Return a unique non-localized ASCII identifier for the exporter.
	virtual std::string GetIdentifier() const;

	// Return the number of extensions supported by the exporter.
	virtual int GetFileExtensionCount() const;

	// Return each extension, in case independent ASCII with no leading dot.
	virtual std::string GetFileExtension(
		int InIndex // index of the extension
	) const;

	// Return a brief description for each extension to populate SketchUp export file type drop-down list.
	virtual std::string GetDescription(
		int InIndex // index of the extension
	) const;

	// Indicate whether the exporter supports an options dialog.
	virtual bool SupportsOptions() const;

	// Display a modal dialog which controls options supported by the exporter.
	virtual void ShowOptionsDialog(
		bool bInModelHasSelection // indicate if the SketchUp model has a current selection set
	);

	// Indicate whether the exporter supports exporting just the selection.
	virtual bool ExportSelectionSetOnly();

	// Indicate whether the exporter supports the progress callback.
	virtual bool SupportsProgress() const;

	// Perform the conversion from a temporary SketchUp input file to a Datasmith output file using options set during the ShowOptionsDialog method,
	// and return true on success, or false on failure or cancellation.
	virtual bool ConvertFromSkp(
		const std::string&              InInputPath,        // temporary SketchUp input file path in UTF-8
		const std::string&              InOutputPath,       // Datasmith output file path in UTF-8
		SketchUpPluginProgressCallback* InProgressCallback, // SketchUp progress interface callback (can be null)
		void*                           InReserved          // reserved for SketchUp internal use
	);

	// Display a modal dialog showing a summary of the last export process.
	virtual void ShowSummaryDialog();
};


inline std::string FDatasmithSketchUpExporterInterface::GetIdentifier() const
{
	// Follow the SketchUp naming convention.
	return "com.sketchup.exporters.udatasmith";
}

inline int FDatasmithSketchUpExporterInterface::GetFileExtensionCount() const
{
	return 1;
}

inline std::string FDatasmithSketchUpExporterInterface::GetFileExtension(
	int /* InIndex */
) const
{
	// Follow the SketchUp lowercase naming convention.
	return "udatasmith";
}

inline std::string FDatasmithSketchUpExporterInterface::GetDescription(
	int /* InIndex */
) const
{
	// Follow the SketchUp lowercase naming convention.
	return "Unreal Datasmith (*.udatasmith)";
}

inline bool FDatasmithSketchUpExporterInterface::SupportsOptions() const
{
	// Hide the Options dialog button for the time being.
	return false;
}

inline bool FDatasmithSketchUpExporterInterface::ExportSelectionSetOnly()
{
	// Always export the complete model whatever its selection state.
	return false;
}

inline bool FDatasmithSketchUpExporterInterface::SupportsProgress() const
{
	return true;
}
