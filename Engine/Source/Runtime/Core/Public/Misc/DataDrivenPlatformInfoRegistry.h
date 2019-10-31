// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Misc/ConfigCacheIni.h"


struct CORE_API FDataDrivenPlatformInfoRegistry
{
	// Information about a platform loaded from disk
	struct FPlatformInfo
	{
		// is this platform confidential
		bool bIsConfidential = false;

		// should this platform be split when using ELocTextPlatformSplitMode::Restricted (only used when bIsConfidential is true)
		bool bRestrictLocalization = false;

		// list of additonal restricted folders
		TArray<FString> AdditionalRestrictedFolders;

		// cached list of ini parents
		TArray<FString> IniParentChain;
	};

	/**
	* Get the global set of data driven platform information
	*/
	static const TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& GetAllPlatformInfos();

	/**
	 * Get the data driven platform info for a given platform. If the platform doesn't have any on disk,
	 * this will return a default constructed FConfigDataDrivenPlatformInfo
	 */
	static const FPlatformInfo& GetPlatformInfo(const FString& PlatformName);

	/**
	 * Gets a list of all known confidential platforms (note these are just the platforms you have access to, so, for example PS4 won't be
	 * returned if you are not a PS4 licensee)
	 */
	static const TArray<FString>& GetConfidentialPlatforms();

	/**
	 * Returns the number of discovered ini files that can be loaded with LoadDataDrivenIniFile
	 */
	static int32 GetNumDataDrivenIniFiles();

	/**
	 * Load the given ini file, and 
	 */
	static bool LoadDataDrivenIniFile(int32 Index, FConfigFile& IniFile, FString& PlatformName);
};

