// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"



static const TArray<FString>& GetDataDrivenIniFilenames()
{
	static bool bHasSearchedForFiles = false;
	static TArray<FString> DataDrivenIniFilenames;

	if (bHasSearchedForFiles == false)
	{
		bHasSearchedForFiles = true;

		// look for the special files in any congfig subdirectories
		IFileManager::Get().FindFilesRecursive(DataDrivenIniFilenames, *FPaths::EngineConfigDir(), TEXT("DataDrivenPlatformInfo.ini"), true, false);
		IFileManager::Get().FindFilesRecursive(DataDrivenIniFilenames, *FPaths::PlatformExtensionsDir(), TEXT("DataDrivenPlatformInfo.ini"), true, false, false);
	}

	return DataDrivenIniFilenames;
}

int32 FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles()
{
	return GetDataDrivenIniFilenames().Num();
}

bool FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(int32 Index, FConfigFile& IniFile, FString& PlatformName)
{
	const TArray<FString>& IniFilenames = GetDataDrivenIniFilenames();
	if (Index < 0 || Index >= IniFilenames.Num())
	{
		return false;
	}

	// manually load a FConfigFile object from a source ini file so that we don't do any SavedConfigDir processing or anything
	// (there's a possibility this is called before the ProjectDir is set)
	FString IniContents;
	if (FFileHelper::LoadFileToString(IniContents, *IniFilenames[Index]))
	{
		IniFile.ProcessInputFileContents(IniContents);

		// platform extension paths are different (platform/engine/config, not engine/config/platform)
		if (IniFilenames[Index].StartsWith(FPaths::PlatformExtensionsDir()))
		{
			PlatformName = FPaths::GetCleanFilename(FPaths::GetPath(FPaths::GetPath(FPaths::GetPath(IniFilenames[Index]))));
		}
		else
		{
			// this could be 'Engine' for a shared DataDrivenPlatformInfo file
			PlatformName = FPaths::GetCleanFilename(FPaths::GetPath(IniFilenames[Index]));
		}

		return true;
	}

	return false;
}

/**
* Get the global set of data driven platform information
*/
static const TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& GetAllPlatformInfos()
{
	static bool bHasSearchedForPlatforms = false;
	static TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo> DataDrivenPlatforms;

	// look on disk for special files
	if (bHasSearchedForPlatforms == false)
	{
		bHasSearchedForPlatforms = true;

		int32 NumFiles = FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles();

		TMap<FString, FString> IniParents;
		for (int32 Index = 0; Index < NumFiles; Index++)
		{
			// load the .ini file
			FConfigFile IniFile;
			FString PlatformName;
			FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformName);

			// platform info is registered by the platform name
			if (IniFile.Contains(TEXT("DataDrivenPlatformInfo")))
			{
				// cache info
				FDataDrivenPlatformInfoRegistry::FPlatformInfo& Info = DataDrivenPlatforms.Add(PlatformName, FDataDrivenPlatformInfoRegistry::FPlatformInfo());
				IniFile.GetBool(TEXT("DataDrivenPlatformInfo"), TEXT("bIsConfidential"), Info.bIsConfidential);
				IniFile.GetBool(TEXT("DataDrivenPlatformInfo"), TEXT("bRestrictLocalization"), Info.bRestrictLocalization);

				// get the parent to build list later
				FString IniParent;
				IniFile.GetString(TEXT("DataDrivenPlatformInfo"), TEXT("IniParent"), IniParent);
				IniParents.Add(PlatformName, IniParent);
			}
		}

		// now that all are read in, calculate the ini parent chain, starting with parent-most
		for (auto& It : DataDrivenPlatforms)
		{
			// walk up the chain and build up the ini chain of parents
			for (FString CurrentPlatform = IniParents.FindRef(It.Key); CurrentPlatform != TEXT(""); CurrentPlatform = IniParents.FindRef(CurrentPlatform))
			{
				// insert at 0 to reverse the order
				It.Value.IniParentChain.Insert(CurrentPlatform, 0);
			}
		}
	}

	return DataDrivenPlatforms;
}


const FDataDrivenPlatformInfoRegistry::FPlatformInfo& FDataDrivenPlatformInfoRegistry::GetPlatformInfo(const FString& PlatformName)
{
	const FPlatformInfo* Info = GetAllPlatformInfos().Find(PlatformName);
	static FPlatformInfo Empty;
	return Info ? *Info : Empty;
}


const TArray<FString>& FDataDrivenPlatformInfoRegistry::GetConfidentialPlatforms()
{
	static bool bHasSearchedForPlatforms = false;
	static TArray<FString> FoundPlatforms;

	// look on disk for special files
	if (bHasSearchedForPlatforms == false)
	{
		for (auto It : GetAllPlatformInfos())
		{
			if (It.Value.bIsConfidential)
			{
				FoundPlatforms.Add(It.Key);
			}
		}

		bHasSearchedForPlatforms = true;
	}

	// return whatever we have already found
	return FoundPlatforms;
}