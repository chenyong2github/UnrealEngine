// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeTestsModule.h"
#include "InterchangeTestsLog.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "InterchangeImportTestSettings.h"


#define LOCTEXT_NAMESPACE "InterchangeEditorModule"

DEFINE_LOG_CATEGORY(LogInterchangeTests);


FInterchangeTestsModule& FInterchangeTestsModule::Get()
{
	return FModuleManager::LoadModuleChecked<FInterchangeTestsModule>(INTERCHANGETESTS_MODULE_NAME);
}


bool FInterchangeTestsModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(INTERCHANGETESTS_MODULE_NAME);
}


void FInterchangeTestsModule::StartupModule()
{
}


TArray<FString> FInterchangeTestsModule::GetImportTests() const
{
	const UInterchangeImportTestSettings* Settings = GetDefault<UInterchangeImportTestSettings>();
	FString ContentDir = FPaths::ProjectContentDir();
	FString Path = FPaths::Combine(ContentDir, Settings->ImportTestsPath);

	TArray<FString> FilesInDirectory;
	const bool bFindFiles = true;
	const bool bFindDirectories = false;
	IFileManager::Get().FindFilesRecursive(FilesInDirectory, *Path, TEXT("*.*"), bFindFiles, bFindDirectories);

	TArray<FString> Results;
	Results.Reserve(FilesInDirectory.Num());

	for (FString& File : FilesInDirectory)
	{
		if (FPaths::GetExtension(File) == TEXT("json"))
		{
			Results.Emplace(MoveTemp(File));
		}
	}

	return Results;
}


IMPLEMENT_MODULE(FInterchangeTestsModule, InterchangeTests);


#undef LOCTEXT_NAMESPACE

