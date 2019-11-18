// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithVREDImportOptions.h"

#include "DatasmithAssetImportData.h"
#include "DatasmithVREDTranslatorModule.h"
#include "Misc/Paths.h"

#include "CoreTypes.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "DatasmithVREDImporter"

UDatasmithVREDImportOptions::UDatasmithVREDImportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bOptimizeDuplicatedNodes(false)
	, bImportMats(true)
	, bImportVar(true)
	, bCleanVar(true)
	, bImportLightInfo(true)
	, bImportClipInfo(true)
{
}

namespace VREDImportOptionsImpl
{
	FString FindBestFile(const FString& FBXFileWithoutExt, const FString& Extension)
	{
		const FString FBXDirectory = FPaths::GetPath(FBXFileWithoutExt);

		FString VarPathStr = FPaths::SetExtension(FBXFileWithoutExt, Extension);
		if (FPaths::FileExists(VarPathStr))
		{
			return VarPathStr;
		}
		else
		{
			TArray<FString> VarFiles;
			IFileManager::Get().FindFiles(VarFiles, *FBXDirectory, *Extension);
			if (VarFiles.Num() > 0)
			{
				return FBXDirectory / VarFiles[0];
			}
		}

		return FString();
	}
}

void UDatasmithVREDImportOptions::ResetPaths(const FString& InFBXFilename, bool bJustEmptyPaths)
{
	// Handle file.fbx and file.fbx.intermediate
	FString PathNoExt = FPaths::ChangeExtension(FPaths::ChangeExtension(InFBXFilename, ""), "");

	if (MatsPath.FilePath.IsEmpty() || !bJustEmptyPaths)
	{
		MatsPath.FilePath = VREDImportOptionsImpl::FindBestFile(PathNoExt, TEXT("mats"));
	}
	if (VarPath.FilePath.IsEmpty() || !bJustEmptyPaths)
	{
		VarPath.FilePath = VREDImportOptionsImpl::FindBestFile(PathNoExt, TEXT("var"));
	}
	if (LightInfoPath.FilePath.IsEmpty() || !bJustEmptyPaths)
	{
		LightInfoPath.FilePath = VREDImportOptionsImpl::FindBestFile(PathNoExt, TEXT("lights"));
	}
	if (ClipInfoPath.FilePath.IsEmpty() || !bJustEmptyPaths)
	{
		ClipInfoPath.FilePath = VREDImportOptionsImpl::FindBestFile(PathNoExt, TEXT("clips"));
	}

	if (TextureDirs.Num() == 0 || !bJustEmptyPaths)
	{
		FString TexturesDirStr = FPaths::GetPath(PathNoExt) / TEXT("Textures");
		if (FPaths::DirectoryExists(TexturesDirStr))
		{
			TextureDirs.SetNum(1);
			TextureDirs[0].Path = TexturesDirStr;
		}
	}
}

void UDatasmithVREDImportOptions::FromSceneImportData(UDatasmithFBXSceneImportData* InImportData)
{
	UDatasmithFBXImportOptions::ToSceneImportData(InImportData);

	if (UDatasmithVREDSceneImportData* VREDImportData = Cast<UDatasmithVREDSceneImportData>(InImportData))
	{
		bOptimizeDuplicatedNodes	= VREDImportData->bOptimizeDuplicatedNodes;
		bImportMats					= VREDImportData->bImportMats;
		MatsPath.FilePath			= VREDImportData->MatsPath;
		bImportVar					= VREDImportData->bImportVar;
		bCleanVar					= VREDImportData->bCleanVar;
		VarPath.FilePath			= VREDImportData->VarPath;
		bImportLightInfo			= VREDImportData->bImportLightInfo;
		LightInfoPath.FilePath		= VREDImportData->LightInfoPath;
		bImportClipInfo				= VREDImportData->bImportClipInfo;
		ClipInfoPath.FilePath		= VREDImportData->ClipInfoPath;
	}
}

void UDatasmithVREDImportOptions::ToSceneImportData(UDatasmithFBXSceneImportData* OutImportData)
{
	UDatasmithFBXImportOptions::ToSceneImportData(OutImportData);

	if (UDatasmithVREDSceneImportData* VREDImportData = Cast<UDatasmithVREDSceneImportData>(OutImportData))
	{
		VREDImportData->bOptimizeDuplicatedNodes	= bOptimizeDuplicatedNodes;
		VREDImportData->bImportMats					= bImportMats;
		VREDImportData->MatsPath					= MatsPath.FilePath;
		VREDImportData->bImportVar					= bImportVar;
		VREDImportData->bCleanVar					= bCleanVar;
		VREDImportData->VarPath						= VarPath.FilePath;
		VREDImportData->bImportLightInfo			= bImportLightInfo;
		VREDImportData->LightInfoPath				= LightInfoPath.FilePath;
		VREDImportData->bImportClipInfo				= bImportClipInfo;
		VREDImportData->ClipInfoPath				= ClipInfoPath.FilePath;
	}
}

#undef LOCTEXT_NAMESPACE
