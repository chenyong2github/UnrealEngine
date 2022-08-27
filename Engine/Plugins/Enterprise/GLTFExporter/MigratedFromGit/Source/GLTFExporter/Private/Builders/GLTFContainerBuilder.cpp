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

void FGLTFContainerBuilder::WriteGlb(FArchive& Archive) const
{
	FBufferArchive JsonData;
	WriteJson(JsonData);

	const TArray<uint8>& BufferData = GetBufferData();

	FGLTFContainerUtility::WriteGlb(Archive, JsonData, BufferData);
}

void FGLTFContainerBuilder::BundleWebViewer()
{
	const FString PluginDir = IPluginManager::Get().FindPlugin(GLTFEXPORTER_MODULE_NAME)->GetBaseDir();
	const FString ArchiveFile = PluginDir / TEXT("Resources") / TEXT("GLTFWebViewer.zip");

	if (!FPaths::FileExists(ArchiveFile))
	{
		AddWarningMessage(FString::Printf(TEXT("No web viewer found at %s"), *ArchiveFile));
		return;
	}

	if (!FGLTFZipUtility::ExtractToDirectory(ArchiveFile, DirPath))
	{
		AddErrorMessage(FString::Printf(TEXT("Failed to extract web viewer at %s"), *ArchiveFile));
		return;
	}

	UpdateWebViewerIndex();
}

void FGLTFContainerBuilder::UpdateWebViewerIndex()
{
	const FString IndexFile = DirPath / TEXT("index.json");

	if (!FPaths::FileExists(IndexFile))
	{
		AddWarningMessage(FString::Printf(TEXT("No index file found at %s"), *IndexFile));
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	if (!ReadJsonFile(IndexFile, JsonObject))
	{
		AddWarningMessage(FString::Printf(TEXT("Failed to read index file at %s"), *IndexFile));
		return;
	}

	JsonObject->SetArrayField(TEXT("assets"), { MakeShared<FJsonValueString>(FPaths::GetCleanFilename(FilePath)) });

	if (!WriteJsonFile(IndexFile, JsonObject.ToSharedRef()))
	{
		AddWarningMessage(FString::Printf(TEXT("Failed to write index file at %s"), *IndexFile));
	}
}

bool FGLTFContainerBuilder::ReadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& JsonObject)
{
	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *FilePath))
	{
		return false;
	}

	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonContent);
	return FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid();
}

bool FGLTFContainerBuilder::WriteJsonFile(const FString& FilePath, const TSharedRef<FJsonObject>& JsonObject)
{
	FString JsonContent;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonContent);

	if (!FJsonSerializer::Serialize(JsonObject, JsonWriter))
	{
		return false;
	}

	return FFileHelper::SaveStringToFile(JsonContent, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8);
}
