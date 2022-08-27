// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFContainerBuilder.h"
#include "Builders/GLTFContainerUtility.h"
#include "GLTFExporterModule.h"
#include "Interfaces/IPluginManager.h"

FGLTFContainerBuilder::FGLTFContainerBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFConvertBuilder(FilePath, ExportOptions, bSelectedActorsOnly)
{
}

void FGLTFContainerBuilder::WriteGlb(FArchive& Archive) const
{
	FBufferArchive JsonData;
	WriteJson(JsonData);

	const TArray<uint8>& BufferData = GetBufferData();

	FGLTFContainerUtility::WriteGlb(Archive, JsonData, BufferData);
}

void FGLTFContainerBuilder::BundleWebViewer()
{
	// TODO: instead of raw files, store web viewer as a zip and unpack it when needed

	const FString WebViewerPath = IPluginManager::Get().FindPlugin(GLTFEXPORTER_MODULE_NAME)->GetBaseDir() / TEXT("Resources") / TEXT("WebViewer");
	TArray<FString> SourceFiles;

	IFileManager& FileManager = IFileManager::Get();
	FileManager.FindFilesRecursive(SourceFiles, *WebViewerPath, TEXT("*"), true, false);

	if (SourceFiles.Num() == 0)
	{
		AddErrorMessage(FString::Printf(TEXT("No source files for web viewer found at %s"), *WebViewerPath));
		return;
	}

	for (const FString& SourceFile : SourceFiles)
	{
		FString DestinationFile = DirPath / SourceFile.RightChop(WebViewerPath.Len());
		if (FileManager.Copy(*DestinationFile, *SourceFile) != COPY_OK)
		{
			AddErrorMessage(FString::Printf(TEXT("Failed to copy web viewer file from %s to %s"), *SourceFile, *DestinationFile));
		}
	}
}

void FGLTFContainerBuilder::Write(FArchive& Archive, FFeedbackContext* Context)
{
	CompleteAllTasks(Context);

	if (bIsGlbFile)
	{
		WriteGlb(Archive);
	}
	else
	{
		WriteJson(Archive);
	}

	if (ExportOptions->bBundleWebViewer)
	{
		BundleWebViewer();
	}
}
