// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFContainerBuilder.h"
#include "Builders/GLTFContainerUtility.h"
#include "Builders/GLTFZipUtility.h"
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
	const FString WebViewerPath = IPluginManager::Get().FindPlugin(GLTFEXPORTER_MODULE_NAME)->GetBaseDir() / TEXT("Resources") / TEXT("WebViewer.zip");
	if (!FPaths::FileExists(WebViewerPath))
	{
		AddWarningMessage(FString::Printf(TEXT("No web viewer found at %s"), *WebViewerPath));
		return;
	}

	if (!FGLTFZipUtility::ExtractToDirectory(WebViewerPath, DirPath, *this))
	{
		AddErrorMessage(FString::Printf(TEXT("Failed to extract web viewer at %s"), *WebViewerPath));
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
