// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLevelInfo.h"

#include "LevelExporterUSDOptions.h"

#include "Engine/World.h"
#include "Exporters/Exporter.h"
#include "HAL/FileManager.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Paths.h"
#include "UObject/GCObjectScopeGuard.h"

ADEPRECATED_USDLevelInfo::ADEPRECATED_USDLevelInfo( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	FileScale = 1.0;
}

void ADEPRECATED_USDLevelInfo::SaveUSD()
{
	UAssetExportTask* LevelExportTask = NewObject<UAssetExportTask>();
	FGCObjectScopeGuard ExportTaskGuard( LevelExportTask );

	LevelExportTask->Object = GWorld;
	LevelExportTask->Options = GetMutableDefault<ULevelExporterUSDOptions>();
	LevelExportTask->Exporter = nullptr;
	LevelExportTask->Filename = FilePath.FilePath;
	LevelExportTask->bSelected = false;
	LevelExportTask->bReplaceIdentical = true;
	LevelExportTask->bPrompt = false;
	LevelExportTask->bUseFileArchive = false;
	LevelExportTask->bWriteEmptyFiles = false;
	LevelExportTask->bAutomated = true;
	UExporter::RunAssetExportTask( LevelExportTask );
}

