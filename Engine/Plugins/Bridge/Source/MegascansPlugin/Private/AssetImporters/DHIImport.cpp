// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetImporters/DHIImport.h"
#include "MSAssetImportData.h"
#include "Utilities/MiscUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ScopedSlowTask.h"
#include "EditorAssetLibrary.h"
#include "GenericPlatform/GenericPlatformFile.h"
//#include "HAL/PlatformFilemanager.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Internationalization/Text.h"
#include "UObject/Object.h"

TSharedPtr<FImportDHI> FImportDHI::ImportDHIInst;

TSharedPtr<FDHIData> FImportDHI::ParseDHIData(TSharedPtr<FJsonObject> AssetImportJson)
{
	TSharedPtr<FDHIData> DHIImportData = MakeShareable( new FDHIData);	
	DHIImportData->CharacterPath = AssetImportJson->GetStringField(TEXT("characterPath"));
	DHIImportData->CharacterName = AssetImportJson->GetStringField(TEXT("folderName"));
	DHIImportData->CommonPath = AssetImportJson->GetStringField(TEXT("commonPath"));
	DHIImportData->CharacterPath = FPaths::Combine(DHIImportData->CharacterPath, DHIImportData->CharacterName);
	return DHIImportData;
	
}

TSharedPtr<FImportDHI> FImportDHI::Get()
{
	if (!ImportDHIInst.IsValid())
	{
		ImportDHIInst = MakeShareable(new FImportDHI);
	}
	return ImportDHIInst;
}

void FImportDHI::ImportAsset(TSharedPtr<FJsonObject> AssetImportJson)
{
	TSharedPtr<FDHIData> CharacterSourceData = ParseDHIData(AssetImportJson);
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FString> AssetsBasePath;
	AssetsBasePath.Add(TEXT("/Game/MetaHumans"));

	FString CommonDestinationPath = FPaths::ProjectContentDir();
	FString MetaHumansRoot = FPaths::Combine(CommonDestinationPath, TEXT("MetaHumans"));
	CommonDestinationPath = FPaths::Combine(MetaHumansRoot, TEXT("Common"));
	FString RootFolder = TEXT("/Game/MetaHumans");
	FString CommonFolder = FPaths::Combine(RootFolder, TEXT("Common"));
	FString CharacterName = CharacterSourceData->CharacterName;
	FString CharacterDestination = FPaths::Combine(MetaHumansRoot, CharacterName);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.DirectoryExists(*CharacterDestination))
	{
		return;

		EAppReturnType::Type ContinueImport = FMessageDialog::Open(EAppMsgType::YesNo, FText(FText::FromString("The character you are trying to import already exists. Do you want to overwrite it.")));
		if (ContinueImport == EAppReturnType::No) return;

	}

	TArray<FString> SourceCommonFiles;
	PlatformFile.FindFilesRecursively(SourceCommonFiles, *CharacterSourceData->CommonPath, NULL);
	TArray<FString> ExistingAssets = UEditorAssetLibrary::ListAssets(TEXT("/Game/MetaHumans/Common"));

	FString ProjectCommonPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MetaHumans"), TEXT("Common")));
	FString SourceCommonPath = CharacterSourceData->CommonPath;
	FPaths::NormalizeDirectoryName(SourceCommonPath);

	TArray<FString> ProjectCommonFiles;
	PlatformFile.FindFilesRecursively(ProjectCommonFiles, *ProjectCommonPath, TEXT("uasset"));
	TArray<FString> ExistingAssetStrippedPaths;

	for (FString ProjectCommonFile : ProjectCommonFiles)
	{
		ExistingAssetStrippedPaths.Add(ProjectCommonFile.Replace(*ProjectCommonPath, TEXT("")));
	}

	TArray<FString> FilesToCopy;
	for (FString FileToCopy : SourceCommonFiles)
	{

		FString NormalizedSourceFile = FileToCopy;
		FPaths::NormalizeFilename(NormalizedSourceFile);
		FString StrippedSourcePath = NormalizedSourceFile.Replace(*SourceCommonPath, TEXT(""));



		if (!ExistingAssetStrippedPaths.Contains(StrippedSourcePath))
		{

			FString CommonFileDestination = FPaths::Combine(CommonDestinationPath, FileToCopy.Replace(*CharacterSourceData->CommonPath, TEXT("")));

			FString FileDirectory = FPaths::GetPath(CommonFileDestination);

			PlatformFile.CreateDirectoryTree(*FileDirectory);

			FString CommonCopyMsg = TEXT("Importing Common Assets.");
			FText CommonCopyMsgDialogMessage = FText::FromString(CommonCopyMsg);
			FScopedSlowTask AssetLoadprogress(1.0f, CommonCopyMsgDialogMessage, true);
			AssetLoadprogress.MakeDialog();
			AssetLoadprogress.EnterProgressFrame(1.0f);
			PlatformFile.CopyFile(*CommonFileDestination, *FileToCopy);


		}
	}



	AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

	PlatformFile.CreateDirectoryTree(*MetaHumansRoot);

	AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

	PlatformFile.CreateDirectoryTree(*CharacterDestination);
	AssetsBasePath.Add("/Game/MetaHumans/" + CharacterSourceData->CharacterName);

	AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

	FString CharacterCopyMsg = TEXT("Importing : ") + CharacterName;
	FText CharacterCopyMsgDialogMessage = FText::FromString(CharacterCopyMsg);
	FScopedSlowTask CharacterLoadprogress(1.0f, CharacterCopyMsgDialogMessage, true);
	CharacterLoadprogress.MakeDialog();
	CharacterLoadprogress.EnterProgressFrame(1.0f);
	PlatformFile.CopyDirectoryTree(*CharacterDestination, *CharacterSourceData->CharacterPath, true);


	AssetRegistryModule.Get().ScanPathsSynchronous(AssetsBasePath, true);

	FString BPName = TEXT("BP_") + CharacterSourceData->CharacterName;
	BPName += TEXT(".") + BPName;
	FString BPPath = FPaths::Combine(TEXT("/Game/MetaHumans/"), CharacterSourceData->CharacterName, BPName);

	FAssetData CharacterAssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*BPPath));
	UObject* CharacterObject = CharacterAssetData.GetAsset();

	AssetUtils::FocusOnSelected(CharacterDestination);
	


}
