// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "Containers/Array.h"


struct CORE_API FDataDrivenPlatformInfoRegistry
{
	// Information about a platform loaded from disk
	struct FPlatformInfo
	{
		// cached list of ini parents
		TArray<FString> IniParentChain;

		// is this platform confidential
		bool bIsConfidential = false;

		// should this platform be split when using ELocTextPlatformSplitMode::Restricted (only used when bIsConfidential is true)
		bool bRestrictLocalization = false;

		// the name of the ini section to use to load audio compression settings (used at runtime and cooktime)
		FString AudioCompressionSettingsIniSectionName;

		// list of additonal restricted folders
		TArray<FString> AdditionalRestrictedFolders;

		// MemoryFreezing information, matches FPlatformTypeLayoutParameters - defaults are clang, noneditor
		uint32 Freezing_MaxFieldAlignment = 0xffffffff;
		bool Freezing_b32Bit = false;
		bool Freezing_bForce64BitMemoryImagePointers = false;
		bool Freezing_bAlignBases = false;
		bool Freezing_bWithRayTracing = false;

		// NOTE: add more settings here (and read them in in the LoadDDPIIniSettings() function in the .cpp)

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

