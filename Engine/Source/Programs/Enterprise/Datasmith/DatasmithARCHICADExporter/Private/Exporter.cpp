// Copyright Epic Games, Inc. All Rights Reserved.

#include <stddef.h>

#include "WarningsDisabler.h"

DISABLE_SDK_WARNINGS_START

#include "DatasmithSceneExporter.h"
#include "DatasmithSceneXmlWriter.h"

DISABLE_SDK_WARNINGS_END

#include "Exporter.h"
#include "ResourcesIDs.h"

#ifdef TicksPerSecond
	#undef TicksPerSecond
#endif
#include "FileManager.h"
#include "Paths.h"
#include "FileSystem.hpp"
#include "DGFileDialog.hpp"

BEGIN_NAMESPACE_UE_AC

// Constructor
FExporter::FExporter() {}

// Destructor
FExporter::~FExporter() {}

// Export the AC model in the specified file
void FExporter::DoExport(const ModelerAPI::Model& InModel, const API_IOParams& IOParams)
{
	// The exporter
	FDatasmithSceneExporter SceneExporter;
	SceneExporter.PreExport();

	// Archicad, to secure document, do save in a scratch file. And exchange it on success
	// But Datasmith need to save in the real file.
	FString LabelString(GSStringToUE(IOParams.saveFileIOName->GetBase()));

	SceneExporter.SetName(*LabelString);

	IO::Location FileLocation(*IOParams.fileLoc);
	FileLocation.DeleteLastLocalName();
	SceneExporter.SetOutputPath(GSStringToUE(FileLocation.ToDisplayText()));

	// Setup our progression
	bool		 OutUserCancelled = false;
	FProgression Progression(kStrListProgression, kExportTitle, kNbPhases, FProgression::kThrowOnCancel,
							 &OutUserCancelled);

	FSyncDatabase SyncDatabase(*LabelString, SceneExporter.GetAssetsOutputPath());

	FSyncContext SyncContext(InModel, SyncDatabase, &Progression);

	TSharedRef< IDatasmithScene > Scene = SyncDatabase.GetScene();

	SyncDatabase.SetSceneInfo();
	SyncDatabase.Synchronize(SyncContext);

	SyncContext.NewPhase(kExportSaving);

	/* Archicad, to secure document, do save in a scratch file. And exchange it on success
	 * But Datasmith require to save in the real file. So we have to swap twice. */
	UE_AC_TraceF("FileLocation=%s\n", FileLocation.ToDisplayText().ToUtf8());
	IO::Folder parentFolder(FileLocation);
	IO::Name   ScratchName;
	IOParams.fileLoc->GetLastLocalName(&ScratchName);
	if (!IOParams.saveFileIOName->IsEmpty())
	{
		GSErrCode GSErr = parentFolder.Exchange(ScratchName, *IOParams.saveFileIOName, IO::AccessDeniedIsError);
		if (GSErr != NoError)
		{
			UE_AC_DebugF("FExporter::DoExport - Exchange 1 returned error %d", GSErr);
		}
	}

	// Datasmith do the save
	SceneExporter.Export(Scene);
	SyncContext.Stats.Print();

	// Swap so that Archicad will re-swap just after.
	GSErrCode GSErr = parentFolder.Exchange(ScratchName, *IOParams.saveFileIOName, IO::AccessDeniedIsError);
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FExporter::DoExport - Exchange 2 returned error %d", GSErr);
	}
}

// Export the AC model in the specified file
void FExporter::DoExport(const ModelerAPI::Model& InModel, const IO::Location& InDestFile)
{
	// The exporter
	FDatasmithSceneExporter SceneExporter;
	SceneExporter.PreExport();

	// Archicad, to secure document, do save in a scratch file. And exchange it on success
	// But Datasmith need to save in the real file.
	IO::Name FileName;
	InDestFile.GetLastLocalName(&FileName);
	FString LabelString(GSStringToUE(FileName.GetBase()));

	SceneExporter.SetName(*LabelString);

	IO::Location FileLocation(InDestFile);
	FileLocation.DeleteLastLocalName();
	SceneExporter.SetOutputPath(GSStringToUE(FileLocation.ToDisplayText()));

	// Setup our progression
	bool		 OutUserCancelled = false;
	FProgression Progression(kStrListProgression, kExportTitle, kNbPhases, FProgression::kThrowOnCancel,
							 &OutUserCancelled);

	FSyncDatabase SyncDatabase(*LabelString, SceneExporter.GetAssetsOutputPath());

	FSyncContext SyncContext(InModel, SyncDatabase, &Progression);

	TSharedRef< IDatasmithScene > Scene = SyncDatabase.GetScene();

	SyncDatabase.SetSceneInfo();
	SyncDatabase.Synchronize(SyncContext);

	SyncContext.NewPhase(kExportSaving);

	// Datasmith do the save
	SceneExporter.Export(Scene);
	SyncContext.Stats.Print();
}

// Export the AC model in the specified file
GSErrCode FExporter::DoChooseDestination(IO::Location* OutDestFile)
{
	DG::FileDialog fileDialog(DG::FileDialog::Save);

	IO::fileSystem.GetSpecialLocation(IO::FileSystem::CurrentFolder, OutDestFile);

	FTM::FileTypeManager templateFileFTM("TemplateFileFTM");
	FTM::TypeID			 datasmithTypeID =
		templateFileFTM.AddType(FTM::FileType("Datasmith file", "udatasmith", 0, 0, -1, NULL));

	fileDialog.SetTitle("Export BCF File");
	fileDialog.AddFilter(datasmithTypeID);
	fileDialog.AddFilter(FTM::RootGroup);
	fileDialog.SelectFilter(0);
	fileDialog.SetFolder(*OutDestFile);

	if (!fileDialog.Invoke())
		return Cancel;

	*OutDestFile = fileDialog.GetSelectedFile();

	return NoError;
}

END_NAMESPACE_UE_AC
