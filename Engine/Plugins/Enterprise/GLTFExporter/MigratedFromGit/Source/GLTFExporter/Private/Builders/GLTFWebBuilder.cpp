// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFWebBuilder.h"
#include "Builders/GLTFFileUtility.h"
#include "Builders/GLTFZipUtility.h"

FGLTFWebBuilder::FGLTFWebBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFContainerBuilder(FilePath, ExportOptions, bSelectedActorsOnly)
{
}

void FGLTFWebBuilder::Write(FArchive& Archive, FFeedbackContext* Context)
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
		const FString ResourcesDir = FGLTFFileUtility::GetPluginDir() / TEXT("Resources");
		BundleWebViewer(ResourcesDir);
		BundleLaunchHelper(ResourcesDir);
	}
}

void FGLTFWebBuilder::BundleWebViewer(const FString& ResourcesDir)
{
	const FString ArchiveFile = ResourcesDir / TEXT("GLTFWebViewer.zip");

	if (!FPaths::FileExists(ArchiveFile))
	{
		AddWarningMessage(FString::Printf(TEXT("No web viewer archive found at %s"), *ArchiveFile));
		return;
	}

	if (!FGLTFZipUtility::ExtractAllFiles(ArchiveFile, DirPath))
	{
		AddErrorMessage(FString::Printf(TEXT("Failed to extract web viewer files from %s"), *ArchiveFile));
		return;
	}

	const FString IndexFile = DirPath / TEXT("index.json");
	TSharedPtr<FJsonObject> JsonObject;

	if (!FGLTFFileUtility::ReadJsonFile(IndexFile, JsonObject))
	{
		AddWarningMessage(FString::Printf(TEXT("Failed to read web viewer index at %s"), *IndexFile));
		return;
	}

	JsonObject->SetArrayField(TEXT("assets"), { MakeShared<FJsonValueString>(FPaths::GetCleanFilename(FilePath)) });

	if (!FGLTFFileUtility::WriteJsonFile(IndexFile, JsonObject.ToSharedRef()))
	{
		AddWarningMessage(FString::Printf(TEXT("Failed to write web viewer index at %s"), *IndexFile));
	}
}

void FGLTFWebBuilder::BundleLaunchHelper(const FString& ResourcesDir)
{
	const FString ExecutableName = GetLaunchHelperExecutable();
	if (ExecutableName.IsEmpty())
	{
		return;
	}

	const FString ArchiveFile = ResourcesDir / TEXT("GLTFLaunchHelper.zip");

	if (!FPaths::FileExists(ArchiveFile))
	{
		AddWarningMessage(FString::Printf(TEXT("No launch helper archive found at %s"), *ArchiveFile));
		return;
	}

	if (!FGLTFZipUtility::ExtractOneFile(ArchiveFile, ExecutableName, DirPath))
	{
		AddErrorMessage(FString::Printf(TEXT("Failed to extract launch helper file (%s) from %s"), *ExecutableName, *ArchiveFile));
		return;
	}

	const FString ExecutableFile = DirPath / ExecutableName;

	if (!FGLTFFileUtility::SetExecutable(*ExecutableFile, true))
	{
		AddWarningMessage(FString::Printf(TEXT("Failed to make launch helper file executable at %s"), *ExecutableFile));
	}
}

const TCHAR* FGLTFWebBuilder::GetLaunchHelperExecutable()
{
#if PLATFORM_WINDOWS
	return TEXT("GLTFLaunchHelper.exe");
#elif PLATFORM_MAC
	return TEXT(""); // TODO: add GLTFLaunchHelper for macos
#else
	return TEXT(""); // TODO: add GLTFLaunchHelper for linux
#endif
}
